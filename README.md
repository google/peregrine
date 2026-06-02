# Peregrine

**Peregrine** is an easy-to-use, performant, and general-purpose
asynchronous transport library designed specifically to serve
machine learning workloads on TPU hosts.

### What it does
At its core, Peregrine abstracts away the complex, low-level
networking details (such as RDMA, DPDK, TCP, and managing multiple NICs)
from ML application code. It provides a simple, unified API for
asynchronous data transfers (`READ` and `WRITE` operations) between
local and remote processes.

### Key Features
* **Versatile Media Support:** Seamlessly transfers data across different
types of media, including host DRAM, TPU HBM, and file systems/block storage (SSD/HDD).
* **Multipath & Chunking:** Automatically splits large data requests into smaller,
fixed-size chunks and spreads them across multiple parallel data channels
to maximize network diversity, reliability, and performance.
* **High Concurrency:** Supports concurrent reads from multiple sender
processes and concurrent writes to multiple receiver processes simultaneously.
* **Hardware Locality Awareness:** Steers data movement along the shortest
physical paths by taking CPU, memory, PCIe, and NIC locality into account
for optimal performance.
* **Reliability:** Features built-in chunk tracking and resending mechanisms
to ensure data arrives reliably, even if specific channels drop packets.
* **Dedicated Execution Engine:** Operates on its own threading model to process
transport requests and update statuses asynchronously without blocking the main
ML application threads.
