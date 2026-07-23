# Userspace Verbs Library

This directory will contain `libverbifrost` — the Darwin port of
`libibverbs` and `librdmacm`.

## Goal

Provide a drop-in compatible `ibv_*` API so existing RDMA applications can
run on macOS with minimal changes:

```c
#include <infiniband/verbs.h>  // same headers as Linux

struct ibv_context *ctx = ibv_open_device(ibv_devices[0]);
struct ibv_pd *pd = ibv_alloc_pd(ctx);
struct ibv_mr *mr = ibv_reg_mr(pd, buf, size, IBV_ACCESS_LOCAL_WRITE);
// ... post send/recv, poll CQ, etc.
```

## Status

Phase 3 (not yet started). Depends on Phases 1–2.
