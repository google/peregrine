import socket

from absl.testing import absltest

from src.util import util


class UtilTest(absltest.TestCase):

  def test_find_free_port(self):
    for family in (socket.AF_INET, socket.AF_INET6):
      for tcp in (True, False):
        port = util.find_free_port(family, tcp)
        if port > 0:
          self.assertBetween(value=port, minv=10000, maxv=65535)


if __name__ == "__main__":
  absltest.main()
