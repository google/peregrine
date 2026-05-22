import ctypes
import datetime
import time
from google3.testing.pybase import googletest
from src.api.python import peregrine


class SimpleTest(googletest.TestCase):

  @classmethod
  def wait_for_completion(
      cls,
      transport: peregrine.Transport,
      handle: peregrine.Handle,
      timeout=datetime.timedelta(seconds=1),
      check_interval=datetime.timedelta(milliseconds=10),
  ):
    """Polls the transport handle until completion, throwing on timeout or failure."""
    start_time = time.time()
    while time.time() - start_time < timeout.total_seconds():
      status = transport.poll(handle)
      if peregrine.is_completed(status):
        if status != peregrine.Status.SUCCESS:
          raise RuntimeError(f"Transport failed with status: {status}")
        return
      time.sleep(check_interval.total_seconds())
    raise TimeoutError("Transport operation timed out")

  def test_read(self):
    # Precondition: no single byte matches initially.
    payload = b"Hello, Peregrine Read Integration!"
    lbuf = ctypes.create_string_buffer(len(payload))
    rbuf = ctypes.create_string_buffer(payload)

    self.assertNotEqual(lbuf.raw, rbuf.raw)

    # Initiate C++ Read Operation (self <- peer)
    transport = peregrine.create_transport()
    req = peregrine.Request()
    req.op = peregrine.Op.READ
    req.laddr = ctypes.addressof(lbuf)
    req.raddr = ctypes.addressof(rbuf)
    req.len = len(payload)

    handle = transport.post("localhost:1234", req)

    # Wait for processing
    self.wait_for_completion(transport, handle)

    # Post-condition: lbuf has successfully matched rbuf
    self.assertEqual(lbuf.raw[: len(payload)], payload)

  def test_write(self):
    # Precondition: no single byte matches initially.
    payload = b"Hello, Peregrine Write Integration!"
    lbuf = ctypes.create_string_buffer(payload)
    rbuf = ctypes.create_string_buffer(len(payload))

    self.assertNotEqual(lbuf.raw, rbuf.raw)

    # Initiate C++ Write Operation (self -> peer)
    transport = peregrine.create_transport()
    req = peregrine.Request()
    req.op = peregrine.Op.WRITE
    req.laddr = ctypes.addressof(lbuf)
    req.raddr = ctypes.addressof(rbuf)
    req.len = len(payload)

    handle = transport.post("localhost:1234", req)

    # Wait for processing
    self.wait_for_completion(transport, handle)

    # Post-condition: rbuf has successfully matched lbuf
    self.assertEqual(rbuf.raw[: len(payload)], payload)


if __name__ == "__main__":
  googletest.main()
