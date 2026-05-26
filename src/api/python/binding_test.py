import ctypes
from google3.testing.pybase import googletest
from src.api import peregrine as pg


class BindTest(googletest.TestCase):

  def test_op_enum(self):
    self.assertEqual(pg.Op.READ.value, 1)
    self.assertEqual(pg.Op.WRITE.value, 2)

  def test_status_enum(self):
    self.assertEqual(pg.Status.IN_PROGRESS.value, 1)
    self.assertEqual(pg.Status.SUCCESS.value, 0)
    self.assertEqual(pg.Status.FAILURE.value, -1)

  def test_status_helpers(self):
    self.assertTrue(pg.is_in_progress(pg.Status.IN_PROGRESS))
    self.assertFalse(pg.is_in_progress(pg.Status.SUCCESS))
    self.assertTrue(pg.is_completed(pg.Status.SUCCESS))
    self.assertFalse(pg.is_completed(pg.Status.IN_PROGRESS))

  def test_handle(self):
    h = pg.Handle(1337)
    self.assertEqual(h.value(), 1337)

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

  def test_transport(self):
    lbuf = ctypes.create_string_buffer(1024)
    rbuf = ctypes.create_string_buffer(1024)

    transport = pg.create_transport()
    self.assertIsNotNone(transport)

    req = pg.Request(
        op=pg.Op.READ,
        laddr=ctypes.addressof(lbuf),
        raddr=ctypes.addressof(rbuf),
        len=512,
    )
    handle = transport.post("localhost:12345", req)
    self.assertIsInstance(handle, pg.Handle)

    status = transport.poll(handle)
    self.assertIsInstance(status, pg.Status)


if __name__ == "__main__":
  googletest.main()
