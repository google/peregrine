import ctypes
import datetime
import json
import socket
import time

from absl.testing import absltest

from peregrine.src.api import peregrine as pg
from peregrine.src.util import util


class BindTest(absltest.TestCase):

  def test_handle(self):
    h = pg.Handle(1337)
    self.assertEqual(h.value(), 1337)

  def test_op_enum(self):
    self.assertEqual(pg.Op.READ.value, 1)
    self.assertEqual(pg.Op.WRITE.value, 2)

  def test_transport_type_enum(self):
    self.assertEqual(pg.TransportType.TCP.value, 1)
    self.assertEqual(pg.TransportType.RDMA.value, 2)

  def test_request(self):
    # Allocate safe virtual buffer memory addresses
    lbuf = ctypes.create_string_buffer(1024)
    rbuf = ctypes.create_string_buffer(1024)

    req = pg.Request()
    self.assertFalse(req.is_valid())

    req.op = pg.Op.WRITE
    req.laddr = ctypes.addressof(lbuf)
    req.raddr = ctypes.addressof(rbuf)
    req.len = 1024
    self.assertEqual(req.op, pg.Op.WRITE)
    self.assertEqual(req.laddr, ctypes.addressof(lbuf))
    self.assertEqual(req.raddr, ctypes.addressof(rbuf))
    self.assertEqual(req.len, 1024)
    self.assertTrue(req.is_valid())
    self.assertIn("Write", str(req))
    self.assertIn("Write", repr(req))

  def test_status_enum(self):
    self.assertEqual(pg.Status.IN_PROGRESS.value, 1)
    self.assertEqual(pg.Status.SUCCESS.value, 0)
    self.assertEqual(pg.Status.FAILURE.value, -1)

  def test_status_helpers(self):
    self.assertTrue(pg.is_in_progress(pg.Status.IN_PROGRESS))
    self.assertFalse(pg.is_in_progress(pg.Status.SUCCESS))
    self.assertTrue(pg.is_completed(pg.Status.SUCCESS))
    self.assertFalse(pg.is_completed(pg.Status.IN_PROGRESS))

  def test_transport(self):
    lbuf = ctypes.create_string_buffer(1024)
    rbuf = ctypes.create_string_buffer(1024)

    port1 = util.find_free_port(socket.AF_INET, tcp=True)
    port2 = util.find_free_port(socket.AF_INET, tcp=True)
    self.assertNotEqual(port1, port2)
    local = f"127.0.0.1:{port1}"
    remote = f"127.0.0.1:{port2}"

    local_transport = pg.create_transport(local, num_conns_per_peer=2)
    self.assertIsNotNone(local_transport)
    remote_transport = pg.create_transport(remote, num_conns_per_peer=2)
    self.assertIsNotNone(remote_transport)

    req = pg.Request(
        op=pg.Op.READ,
        laddr=ctypes.addressof(lbuf),
        raddr=ctypes.addressof(rbuf),
        len=512,
    )
    handle = local_transport.post(remote, [req])
    self.assertIsInstance(handle, pg.Handle)

    timeout = datetime.timedelta(seconds=10)
    end_time = time.monotonic() + timeout.total_seconds()
    while time.monotonic() < end_time:
      status = local_transport.poll(handle)
      self.assertIsInstance(status, pg.Status)
      if not pg.is_completed(status):
        time.sleep(0.1)
      elif status == pg.Status.SUCCESS:
        return
      else:
        self.fail(f"transport failed: {status!r}")
    else:
      self.fail("transport timed out")

  def test_register_memory(self):
    lbuf = ctypes.create_string_buffer(4096)
    port = util.find_free_port(socket.AF_INET, tcp=True)
    transport = pg.create_transport(f"127.0.0.1:{port}", num_conns_per_peer=1)
    self.assertIsNotNone(transport)
    # Registration on TCP transport is a no-op that succeeds.
    transport.register_memory(ctypes.addressof(lbuf), len(lbuf))
    transport.unregister_memory(ctypes.addressof(lbuf))

  def test_get_transport_metrics(self):
    port = util.find_free_port(socket.AF_INET, tcp=True)
    transport = pg.create_transport(f"127.0.0.1:{port}", num_conns_per_peer=1)
    self.assertIsNotNone(transport)
    metrics = transport.get_transport_metrics()
    self.assertIsInstance(metrics, pg.TransportMetrics)
    self.assertIsInstance(metrics.write, pg.OpMetrics)
    self.assertIsInstance(metrics.read, pg.OpMetrics)

  def test_tracing(self):
    lbuf = ctypes.create_string_buffer(2048)
    rbuf = ctypes.create_string_buffer(2048)

    port1 = util.find_free_port(socket.AF_INET, tcp=True)
    port2 = util.find_free_port(socket.AF_INET, tcp=True)
    local = f"127.0.0.1:{port1}"
    remote = f"127.0.0.1:{port2}"

    local_transport = pg.create_transport(local, num_conns_per_peer=2)
    self.assertIsNotNone(local_transport)
    remote_transport = pg.create_transport(remote, num_conns_per_peer=2)
    self.assertIsNotNone(remote_transport)
    trace_cfg = pg.TraceConfig(sample_every_n=1)
    local_transport.configure_trace(trace_cfg)

    req = pg.Request(
        op=pg.Op.WRITE,
        laddr=ctypes.addressof(lbuf),
        raddr=ctypes.addressof(rbuf),
        len=2048,
    )
    handle = local_transport.post(remote, [req])
    end_time = time.monotonic() + 10.0
    while time.monotonic() < end_time:
      status = local_transport.poll(handle)
      if pg.is_completed(status):
        self.assertEqual(status, pg.Status.SUCCESS)
        break
      time.sleep(0.05)
    else:
      self.fail("transport timed out")

    events = json.loads(local_transport.export_chrome_trace_json())[
        "traceEvents"
    ]
    conn_events = [
        ev
        for ev in events
        if ev["name"].startswith("Engine::createSendWorkers")
    ]
    self.assertLen(conn_events, 1)
    self.assertGreater(conn_events[0]["args"]["conn_setup_us"], 0)

    send_events = [ev for ev in events if ev["name"].startswith("SendChunk(")]
    self.assertLen(send_events, 2)

    all_done_events = [
        ev for ev in events if ev["name"].startswith("kAllRequestsDone")
    ]
    self.assertLen(all_done_events, 1)
    self.assertEqual(all_done_events[0]["args"]["handle"], hex(handle.value()))
    transport_e2e_us = all_done_events[0]["args"]["transport_e2e_us"]
    self.assertGreater(transport_e2e_us, 0)

    req_done_events = [
        ev for ev in events if ev["name"].startswith("kRequestDone")
    ]
    self.assertLen(req_done_events, 1)
    request_e2e_us = req_done_events[0]["args"]["request_e2e_us"]
    self.assertGreater(request_e2e_us, 0)
    self.assertLessEqual(request_e2e_us, transport_e2e_us)

    poll_events = [
        ev for ev in events if ev["name"].startswith("TransportImpl::Poll(")
    ]
    self.assertLen(poll_events, 1)
    self.assertIn("completion_pickup_us", poll_events[0]["args"])

    local_transport.clear_trace()
    cleared_events = json.loads(local_transport.export_chrome_trace_json())[
        "traceEvents"
    ]
    self.assertEmpty(
        [ev for ev in cleared_events if ev["cat"] != "__metadata"]
    )


if __name__ == "__main__":
  absltest.main()
