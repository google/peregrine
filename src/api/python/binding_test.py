import ctypes
import datetime
import socket
import time

from absl.testing import absltest

from src.api import peregrine as pg
from src.util import util


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

    metric_fields = [
        attr for attr in dir(metrics) if not attr.startswith("_")
    ]
    self.assertNotEmpty(metric_fields)
    self.assertIn("tcp_connect_failures", metric_fields)
    self.assertIn("rpc_requests_received", metric_fields)
    for field in metric_fields:
      val = getattr(metrics, field)
      if not callable(val):
        self.assertIsInstance(val, int)
        self.assertGreaterEqual(val, 0)


if __name__ == "__main__":
  absltest.main()
