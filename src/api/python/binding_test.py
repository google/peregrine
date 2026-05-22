import ctypes
from google3.testing.pybase import googletest
from src.api.python import peregrine


class BindTest(googletest.TestCase):

  def test_op_enum(self):
    self.assertEqual(peregrine.Op.READ.value, 1)
    self.assertEqual(peregrine.Op.WRITE.value, 2)

  def test_status_enum(self):
    self.assertEqual(peregrine.Status.IN_PROGRESS.value, 1)
    self.assertEqual(peregrine.Status.SUCCESS.value, 0)
    self.assertEqual(peregrine.Status.FAILURE.value, -1)

  def test_status_helpers(self):
    self.assertTrue(peregrine.is_in_progress(peregrine.Status.IN_PROGRESS))
    self.assertFalse(peregrine.is_in_progress(peregrine.Status.SUCCESS))
    self.assertTrue(peregrine.is_completed(peregrine.Status.SUCCESS))
    self.assertFalse(peregrine.is_completed(peregrine.Status.IN_PROGRESS))

  def test_handle(self):
    h = peregrine.Handle(1337)
    self.assertEqual(h.value(), 1337)

  def test_request(self):
    req1 = peregrine.Request()
    self.assertFalse(req1.is_valid())

    # Allocate safe virtual buffer memory addresses
    lbuf = ctypes.create_string_buffer(1024)
    rbuf = ctypes.create_string_buffer(1024)

    req1.op = peregrine.Op.WRITE
    req1.laddr = ctypes.addressof(lbuf)
    req1.raddr = ctypes.addressof(rbuf)
    req1.len = 1024

    self.assertEqual(req1.op, peregrine.Op.WRITE)
    self.assertEqual(req1.laddr, ctypes.addressof(lbuf))
    self.assertEqual(req1.raddr, ctypes.addressof(rbuf))
    self.assertEqual(req1.len, 1024)
    self.assertTrue(req1.is_valid())

    req2 = peregrine.Request()
    req2.op = peregrine.Op.WRITE
    req2.laddr = ctypes.addressof(lbuf)
    req2.raddr = ctypes.addressof(rbuf)
    req2.len = 1024

    self.assertEqual(req1, req2)

    req2.len = 2048
    self.assertNotEqual(req1, req2)

    self.assertIn("op", str(req1))
    self.assertIn("op", repr(req1))

  def test_transport(self):
    transport = peregrine.create_transport()
    self.assertIsNotNone(transport)

    lbuf = ctypes.create_string_buffer(1024)
    rbuf = ctypes.create_string_buffer(1024)

    req = peregrine.Request()
    req.op = peregrine.Op.READ
    req.laddr = ctypes.addressof(lbuf)
    req.raddr = ctypes.addressof(rbuf)
    req.len = 512

    handle = transport.post("localhost:1234", req)
    self.assertIsInstance(handle, peregrine.Handle)

    status = transport.poll(handle)
    self.assertIsInstance(status, peregrine.Status)


if __name__ == "__main__":
  googletest.main()
