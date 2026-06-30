import ctypes
import datetime
import socket
import time

from absl import logging
from absl.testing import absltest

from src.api import peregrine as pg
from src.util import util

_TIMEOUT = datetime.timedelta(seconds=10)
_INTERVAL = datetime.timedelta(milliseconds=100)


class SimpleTest(absltest.TestCase):

  def setUp(self):
    super().setUp()
    self.data = b"Peregrine Python Tests!"
    self.assertNotEqual(len(self.data) % 2, 0)
    port1 = util.find_free_port(socket.AF_INET, tcp=True)
    port2 = util.find_free_port(socket.AF_INET, tcp=True)
    self.assertNotEqual(port1, port2)
    self.local = f"127.0.0.1:{port1}"
    self.remote = f"127.0.0.1:{port2}"
    nconns_per_peer = 3
    self.local_transport = pg.create_transport(self.local, nconns_per_peer)
    self.remote_transport = pg.create_transport(self.remote, nconns_per_peer)
    logging.info("local endpoint listening on %s", self.local)
    logging.info("remote endpoint listening on %s", self.remote)

  def wait_for_completion(self, handle: pg.Handle) -> None:
    end_time = time.time() + _TIMEOUT.total_seconds()
    while time.time() < end_time:
      status = self.local_transport.poll(handle)
      if not pg.is_completed(status):
        time.sleep(_INTERVAL.total_seconds())
      elif status == pg.Status.SUCCESS:
        return
      else:
        self.fail(f"Transport failed: {status!r}")
    else:
      self.fail("Transport timed out")

  def test_read(self):
    # Precondition: local buf doesn't match the expected data.
    rbuf = ctypes.create_string_buffer(self.data)
    lbuf = ctypes.create_string_buffer(len(self.data))
    self.assertNotEqual(lbuf.raw, self.data)

    # Initiate read (self <- peer) and wait for completion.
    req = pg.Request(
        op=pg.Op.READ,
        laddr=ctypes.addressof(lbuf),
        raddr=ctypes.addressof(rbuf),
        len=len(self.data),
    )
    handle = self.local_transport.post(self.remote, [req])
    self.wait_for_completion(handle)

    # Post-condition: local buf matches the expected data.
    self.assertEqual(lbuf.raw, self.data)

  def test_write(self):
    # Precondition: remote buf doesn't match the expected data.
    lbuf = ctypes.create_string_buffer(self.data)
    rbuf = ctypes.create_string_buffer(len(self.data))
    self.assertNotEqual(rbuf.raw, self.data)

    # Initiate write (self -> peer) and wait for completion.
    req = pg.Request(
        op=pg.Op.WRITE,
        laddr=ctypes.addressof(lbuf),
        raddr=ctypes.addressof(rbuf),
        len=len(self.data),
    )
    handle = self.local_transport.post(self.remote, [req])
    self.wait_for_completion(handle)

    # Post-condition: remote buf matches the expected data.
    self.assertEqual(rbuf.raw, self.data)


if __name__ == "__main__":
  absltest.main()
