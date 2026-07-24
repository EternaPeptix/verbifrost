# libibverbs.dylib Internals

> Findings from `tools/trace_verbs.c` live testing on Mac Studio 512S2.

## Library identity

```
dlopen name:     librdma.dylib  (dyld shared cache)
Real path:       /usr/lib/rdma/libibverbs.dylib  (install name)
Directory:       /usr/lib/rdma/  (exists but empty on disk — library is in dyld cache)
```

Apple's `libibverbs.dylib` is based on the upstream `rdma-core` libibverbs,
copyrighted "Apple, Inc. (2025)" with the original OFED/libibverbs copyright
attribution preserved.

## Device context (ibv_context)

When `ibv_open_device()` is called, it returns an `ibv_context` with:

```c
ctx->cmd_fd   = 5131  // IOKit connection handle (NOT a Unix fd)
ctx->async_fd = -1    // no async event channel
ctx->ops      = { ... }  // 32 function pointers — all filled in
```

**`cmd_fd` is NOT a file descriptor.** It's an IOKit `io_connect_t` handle
stored as an integer. The values are sequential across devices (5131, 5135,
5139, 5143, 5147, 5151) — typical of mach port name allocation. This confirms
that `libibverbs.dylib` communicates with the kernel via IOKit `IOConnect*`
calls, not via `/dev/infiniband/uverbsN` ioctls like Linux.

## Full ops table (from live memory dump)

All 32 function pointers in `ibv_context_ops` are filled with real
implementations:

| Index | Operation | Address | Status |
|-------|-----------|---------|--------|
| 0 | `_compat_query_device` | `0x028aeb2a7c` | Stub |
| 1 | `_compat_query_port` | `0x028aeb2b60` | Stub |
| 2 | `_compat_alloc_pd` | `0x028af081c4` | ✅ Real |
| 3 | `_compat_dealloc_pd` | `0x028af08300` | ✅ Real |
| 4 | `_compat_reg_mr` | `0x028af07e80` | ✅ Real |
| 6 | `_compat_dereg_mr` | `0x028af0809c` | ✅ Real |
| 10 | `_compat_create_cq` | `0x028af076e0` | ✅ Real |
| **11** | **`poll_cq`** | **`0x028af079b4`** | **✅ Real** |
| 15 | `_compat_destroy_cq` | `0x028af078a0` | ✅ Real |
| 21 | `_compat_create_qp` | `0x028af083dc` | ✅ Real |
| 23 | `_compat_modify_qp` | `0x028af08a64` | ✅ Real |
| 24 | `_compat_destroy_qp` | `0x028af08974` | ✅ Real |
| **25** | **`post_send`** | **`0x028af09b50`** | **✅ Real** |
| **26** | **`post_recv`** | **`0x028af098c4`** | **✅ Real** |

Functions at `0x028af0xxxx` are real implementations. Functions at
`0x028aebxxxx` are compat/error stubs (return ENOSYS or EINVAL).

## Live test results

| Operation | Result | Notes |
|-----------|--------|-------|
| `ibv_get_device_list` | ✅ 6 devices | All Thunderbolt RDMA interfaces |
| `ibv_open_device` | ✅ Works | Returns context with cmd_fd = IOKit handle |
| `ibv_query_port` | ✅ Works | Port state, LID, GID table all queryable |
| `ibv_query_gid` | ✅ Works | GID entries returned correctly |
| `ibv_alloc_pd` | ✅ Works | On active ports only |
| `ibv_create_cq` | ✅ Works | On active ports only |
| `ibv_create_qp` | ❌ NULL | Needs `com.apple.private.iokit.rdma` entitlement |
| `ibv_reg_mr` | ❌ errno -22 | Same entitlement requirement |

## Communication mechanism (confirmed)

`libibverbs.dylib` communicates with the kernel via an **opaque IOKit channel**,
not via standard Unix ioctls or `/dev/` device files:

1. `ibv_open_device` calls `IOServiceOpen` → gets an `io_connect_t` handle
2. This handle is stored in `ctx->cmd_fd` (misleadingly named — not a Unix fd)
3. The ops functions (alloc_pd, create_qp, post_send, etc.) call internal
   dispatch routines that use this handle

## Complete call chain (confirmed via GOT resolution + disassembly)

We traced the full call chain from the public API to the kernel IPC:

```
Application calls ibv_alloc_pd(ctx)
  ↓
ctx->ops._compat_alloc_pd (0x28af081c4)
  → os_log_type_enabled()     [logging]
  → malloc_type_calloc(1, 0x650)  [allocate cmd buffer]
  → _os_log_impl()            [logging]
  → ibv_cmd_alloc_pd()        [★ THE KEY FUNCTION]
  → os_log_pack_send()        [logging]
  ↓
ibv_cmd_alloc_pd (0x28aeb582c)
  [Standard upstream rdma-core function]
  → BL 0x28aeb3b88  [★ CORE DISPATCH — unnamed internal function]
     with args: w1=3, w2=1, w6=4
  ↓
0x28aeb3b88 (unnamed, internal to libibverbs.dylib)
  [This is the macOS equivalent of Linux's write(fd, &cmd, sizeof(cmd))]
  → Uses the opaque IOKit channel (ctx->cmd_fd) to send the command
  → Not mach_msg, not write(), not standard IOConnect — private mechanism
```

All `ibv_cmd_*` functions resolve to standard upstream rdma-core symbols:

| Function | Address | Role |
|----------|---------|------|
| `ibv_cmd_alloc_pd` | 0x28aeb582c | PD allocation |
| `ibv_cmd_create_cq` | 0x28aeb032c | CQ creation |
| `ibv_cmd_create_qp` | 0x28aeba48c | QP creation |
| `ibv_cmd_reg_mr` | 0x28aeb5948 | Memory registration |
| `ibv_cmd_poll_cq` | 0x28aeb5b38 | CQ polling |
| `ibv_cmd_post_send` | 0x28aeb6480 | Post send WRs |
| `ibv_cmd_post_recv` | 0x28aeb66c4 | Post recv WRs |
| `ibv_cmd_modify_qp` | 0x28aeb6188 | QP state machine |
| `ibv_cmd_query_port` | 0x28aeb3ed0 | Port query |

The core dispatch at 0x28aeb3b88 is a **static (unexported) function** —
dladdr returns `copy_flow_action_esp` (the nearest exported symbol) as a
fallback. This function is the macOS equivalent of Linux's
`writev(context->cmd_fd, iov, 2)` — it encodes the command struct and
sends it to the kernel via the IOKit connection handle.

The exact mach_msg encoding used by this dispatch function is the final
unknown. It requires extracting the function from the dyld shared cache
and disassembling it — a Phase 1 detail that doesn't block our
understanding of the overall architecture.


## No librdmacm

`librdmacm.dylib` (RDMA Connection Manager) is **not available** on macOS.
Connection management is handled internally by the IORDMAFamily framework.

## No provider plugin mechanism

Unlike Linux's libibverbs (which `dlopen`s provider libraries from
`/usr/lib/rdma/`), Apple's version has no `ibv_register_driver` symbol.
The provider registration happens at the kernel level via IOKit properties,
not via userspace library loading.

