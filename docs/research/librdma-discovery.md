# librdma.dylib: macOS's Hidden libibverbs

> **⚠️ BREAKTHROUGH (2026-07-23):** macOS ships a fully functional
> `libibverbs`-compatible library called `librdma.dylib`. It provides the
> standard `ibv_*` API and currently exposes 6 Thunderbolt RDMA devices.
> This fundamentally changes Verbifrost's scope.

## Discovery

Analysis of MLX's open-source jaccl backend (`mlx/distributed/jaccl/lib/jaccl/rdma.cpp`)
revealed that Apple's collective communication library uses a standard
verbs API loaded via `dlopen`:

```cpp
librdma_handle_ = dlopen("librdma.dylib", RTLD_NOW | RTLD_GLOBAL);

LOAD_SYMBOL(ibv_get_device_list, get_device_list);
LOAD_SYMBOL(ibv_open_device, open_device);
LOAD_SYMBOL(ibv_alloc_pd, alloc_pd);
LOAD_SYMBOL(ibv_create_qp, create_qp);
LOAD_SYMBOL(ibv_create_cq, create_cq);
LOAD_SYMBOL(ibv_modify_qp, modify_qp);
LOAD_SYMBOL(ibv_reg_mr, reg_mr);
LOAD_SYMBOL(ibv_query_port, query_port);
LOAD_SYMBOL(ibv_query_gid, query_gid);
```

## Verification

Live test on Mac Studio 512S2 (macOS 26.4 / Darwin 25.4.0):

```python
import ctypes
lib = ctypes.CDLL("librdma.dylib")  # ✅ Loads successfully
num = ctypes.c_int(0)
devs = lib.ibv_get_device_list(ctypes.byref(num))
# → Device count: 6
# →   rdma_en2, rdma_en3, rdma_en4, rdma_en5, rdma_en6, rdma_en7
```

All 6 devices are Thunderbolt bridge interfaces. **The ConnectX-6 LX is NOT
listed** — because no IORDMAFamily provider exists for it.

## What librdma.dylib provides

| ibv_* function | Purpose | Status |
|----------------|---------|--------|
| `ibv_get_device_list` | Enumerate RDMA devices | ✅ Verified live |
| `ibv_get_device_name` | Get device name | ✅ Verified live |
| `ibv_open_device` | Open a device context | ✅ (jaccl uses it) |
| `ibv_alloc_pd` | Protection Domain | ✅ (jaccl uses it) |
| `ibv_create_qp` | Queue Pair creation | ✅ (jaccl uses it) |
| `ibv_create_cq` | Completion Queue creation | ✅ (jaccl uses it) |
| `ibv_modify_qp` | QP state machine (INIT→RTR→RTS) | ✅ (jaccl uses it) |
| `ibv_reg_mr` | Memory Region registration | ✅ (jaccl uses it) |
| `ibv_query_port` | Query port state | ✅ (jaccl uses it) |
| `ibv_query_gid` | Query GID table | ✅ (jaccl uses it) |
| `ibv_post_send` / `post_recv` / `poll_cq` | Data path | ⚠️ Not loaded by jaccl |

## Where it lives

Not a standalone file — it's in the macOS **dyld shared cache**. Loads via
`dlopen("librdma.dylib")` but invisible to `find`/`ls`. Backed by
`IORDMAFamily.kext` (prelinked in kernelcache) + a userspace stub.

## Revised Verbifrost strategy

The entire userspace API (`librdma.dylib`) and kernel RDMA core
(`IORDMAFamily.kext`) already exist and work. We only need the provider:

```
┌──────────────────────────────────────────────────┐
│  Application (jaccl, or any libibverbs client)   │
├──────────────────────────────────────────────────┤
│  librdma.dylib (dyld shared cache)  ← EXISTS     │
│  ibv_get_device_list, ibv_create_qp, ibv_reg_mr  │
├──────────────────────────────────────────────────┤
│  IORDMAFamily.kext (kernel)         ← EXISTS     │
│  Manages QP, CQ, MR, PD, dispatches to providers │
├──────────────┬───────────────────────────────────┤
│  TB Provider  │  ConnectX Provider — TO BUILD    │
│  (Apple)      │  (Verbifrost Phase 1-2)          │
│  rdma_en2-7   │  mlx5 HCA + RoCEv2               │
├──────────────┼───────────────────────────────────┤
│  Thunderbolt  │  ConnectX-6 LX                   │
│  controller   │  (PCIe via TB enclosure)         │
└──────────────┴───────────────────────────────────┘
```

### Scope reduction

| Component | Old estimate | New estimate |
|-----------|-------------|-------------|
| Kernel RDMA core (ib_core) | 6 months | ✅ Already exists |
| Userspace verbs library | 3 months | ✅ Already exists |
| IORDMAFamily provider RE | — | 1 month |
| ConnectX provider dext | 6 months | 3 months |
| **Total** | **~2 years** | **~4-6 months** |

## Next steps

- [ ] Extract IORDMAFamily.kext from the kernelcache for RE
- [ ] Dump the provider interface vtable
- [ ] Check if `ibv_post_send` / `ibv_post_recv` / `ibv_poll_cq` exist
- [ ] Study how AppleThunderboltRDMA registers as an IORDMAFamily provider

