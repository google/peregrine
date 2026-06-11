import ctypes
import datetime
import socket
import time

from absl.testing import absltest

from src.api import peregrine as pg
from src.util import util

_TIMEOUT = datetime.timedelta(seconds=10)
_INTERVAL = datetime.timedelta(milliseconds=100)


class SimpleTest(absltest.TestCase):

  def setUp(self):
    super().setUp()
    port1 = util.find_free_port(socket.AF_INET, tcp=True)
    port2 = util.find_free_port(socket.AF_INET, tcp=True)
    self.assertNotEqual(port1, port2)
    self.self = f"127.0.0.1:{port1}"
    self.peer = f"127.0.0.1:{port2}"
    self.transport = pg.create_transport(self.self)

  def wait_for_completion(self, handle: pg.Handle) -> None:
    end_time = time.time() + _TIMEOUT.total_seconds()
    while time.time() < end_time:
      status = self.transport.poll(handle)
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
    data = b"Peregrine Read Integration!"
    rbuf = ctypes.create_string_buffer(data)
    lbuf = ctypes.create_string_buffer(len(data))
    self.assertNotEqual(lbuf.raw, data)

    # Initiate read (self <- peer) and wait for completion.
    req = pg.Request(
        op=pg.Op.READ,
        laddr=ctypes.addressof(lbuf),
        raddr=ctypes.addressof(rbuf),
        len=len(data),
    )
    handle = self.transport.post(self.peer, [req])
    self.wait_for_completion(handle)

    # Post-condition: local buf matches the expected data.
    self.assertEqual(lbuf.raw, data)

  def test_write(self):
    # Precondition: remote buf doesn't match the expected data.
    data = b"Peregrine Write Integration!"
    lbuf = ctypes.create_string_buffer(data)
    rbuf = ctypes.create_string_buffer(len(data))
    self.assertNotEqual(rbuf.raw, data)

    # Initiate write (self -> peer) and wait for completion.
    req = pg.Request(
        op=pg.Op.WRITE,
        laddr=ctypes.addressof(lbuf),
        raddr=ctypes.addressof(rbuf),
        len=len(data),
    )
    handle = self.transport.post(self.peer, [req])
    self.wait_for_completion(handle)

    # Post-condition: remote buf matches the expected data.
    self.assertEqual(rbuf.raw, data)


if __name__ == "__main__":
  absltest.main()
