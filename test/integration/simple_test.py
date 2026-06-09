import ctypes
import datetime
import time
from google3.testing.pybase import googletest
from src.api import peregrine as pg

_TIMEOUT = datetime.timedelta(seconds=10)
_INTERVAL = datetime.timedelta(milliseconds=100)


class SimpleTest(googletest.TestCase):

  def setUp(self):
    super().setUp()
    self.self = "127.0.0.1:12345"
    self.peer = "127.0.0.1:54321"
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
    handle = self.transport.post(self.peer, req)
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
    handle = self.transport.post(self.peer, req)
    self.wait_for_completion(handle)

    # Post-condition: remote buf matches the expected data.
    self.assertEqual(rbuf.raw, data)


if __name__ == "__main__":
  googletest.main()
