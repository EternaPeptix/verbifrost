# Kernel / DriverKit RDMA Transport

This directory will contain the DriverKit dext that implements the RDMA
transport layer — the Darwin equivalent of Linux's `ib_core.ko`.

## Responsibilities

- Register as an IOKit/DriverKit service matching PCI RDMA-capable NICs
- Expose character devices (`/dev/verbifrost/uverbsN`) for userspace verbs
- Manage memory registration (MR), queue pairs (QP), completion queues (CQ)
- Handle DMA mapping for registered memory regions
- Provide the ioctl interface that `libverbifrost` calls

## Status

Phase 1 (not yet started). See [ROADMAP.md](../ROADMAP.md).
