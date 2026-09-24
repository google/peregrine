"""Utility functions for dev/testing."""

import random
import socket


def find_free_port(family: int, tcp: bool) -> int:
  """Finds an unused port in the range [10,000, 65,535], inclusively.

  Args:
    family: The socket family, either socket.AF_INET or socket.AF_INET6.
    tcp: True if TCP, False if UDP.

  Returns:
    The port number if successful, or 0 otherwise.

  Notes:
    There is no guarantee that the found port is still available when the
    caller actually uses it.
  """
  if family not in (socket.AF_INET, socket.AF_INET6):
    raise ValueError("family must be AF_INET or AF_INET6")

  sock_type = socket.SOCK_STREAM if tcp else socket.SOCK_DGRAM
  protocol = socket.IPPROTO_TCP if tcp else socket.IPPROTO_UDP
  min_port, max_port = 10_000, 65_535

  for _ in range(100):
    s = None
    try:
      # Create a socket.
      s = socket.socket(family, sock_type, protocol)

      # Set SO_REUSEADDR to avoid "Address already in use" error.
      s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

      # Bind the socket to a randomly chosen port.
      ip = "0.0.0.0" if family == socket.AF_INET else "::"
      port = random.randint(min_port, max_port)
      s.bind((ip, port))

      # Check the bound socket.
      _, bound_port, *_ = s.getsockname()
      if bound_port != port:
        continue

      # Check that the tcp socket can listen.
      if tcp:
        s.listen(socket.SOMAXCONN)

      return port
    except OSError:
      continue
    finally:
      if s is not None:
        s.close()

  return 0
