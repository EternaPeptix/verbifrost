# IORDMAFamily: Apple's Linux RDMA Subsystem Port

> **MAJOR BREAKTHROUGH (2026-07-23):** Extracted IORDMAFamily.kext and
> AppleThunderboltRDMA.kext binaries from the boot kernelcache.
> String analysis reveals IORDMAFamily is a **direct port of the Linux
> RDMA subsystem (rdma-core kernel modules)** to macOS.

## Source structure (from build paths in binary)

Apple's XBS build paths leak the complete source tree:

```
Sources/RDMA_family/IORDMAFamily/
├── IORDMAFamily/
│   ├── IORDMAInterface.cpp      ← ★ Provider interface (C++ abstract class)
│   └── IORDMAUserClient.cpp     ← UserClient (forwards ibv_* to provider)
├── core/                        ← Linux kernel's drivers/infiniband/core/
│   ├── addr.c, cache.c, device.c
│   ├── ib_core_uverbs.c, netlink.c, rdma_core.c
│   ├── restrack.c, sa_query.c
│   ├── uverbs_cmd.c, uverbs_ioctl.c, uverbs_uapi.c
│   └── verbs.c
└── include/rdma/                ← Linux kernel's include/rdma/
    ├── ib_verbs.h               ← ib_device, ib_device_ops, ib_pd, etc.
    └── uverbs_std_types.h
```

This is essentially the Linux kernel's `drivers/infiniband/core/`
compiled as a macOS kext, with IOKit wrappers.

## Provider interface: IORDMAInterface

The provider interface is the C++ class `IORDMAInterface` — the macOS
equivalent of Linux's `ib_device_ops` struct.

Confirmed class names from binary:
```
IORDMAInterface          ← Provider interface (abstract base class)
IORDMAUserClient         ← UserClient implementation
IORDMAFamilyUC           ← UserClient class name
IORDMAWrite              ← RDMA write operation type
IORDMAController         ← Mentioned in kext Info.plist
```

## Linux RDMA API functions in the binary

All standard Linux RDMA core functions are compiled in:

**Device registration:**
`_ib_register_device`, `_ib_unregister_device`, `_ib_device_get_by_index`,
`_ib_device_get_by_name`, `_ib_device_put`, `_ib_device_release`

**Verbs objects:**
`___ib_alloc_pd`, `__ib_dealloc_pd_user`, `___ib_create_cq`,
`__ib_destroy_cq_user`, `_create_qp`, `__ib_create_qp_user`,
`__ib_close_qp`, `__ib_alloc_mr`, `_ib_alloc_mr_integrity`,
`__ib_dereg_mr_user`, `__rdma_create_ah`

**uverbs path:**
`__ib_uverbs_mmap`, `_ib_uverbs_lookup_mdesc`

**Cache/GID:**
`_ib_cache_setup_one`, `_ib_cache_cleanup_one`, `_ib_cache_update`,
`_ib_cache_gid_add`, `_ib_cache_gid_del`

## What this means for Verbifrost

**The provider interface is the Linux RDMA `ib_device_ops` struct.**

IORDMAFamily is a port of Linux's `drivers/infiniband/core/`, so the
provider registration mechanism is identical:

```c
// Linux mlx5 driver:
struct ib_device *ib_dev = ib_alloc_device(...);
ib_dev->ops = &mlx5_ib_dev_ops;  // Fill in ib_device_ops
ib_register_device(ib_dev, "mlx5_0", NULL);

// macOS ConnectX provider (Verbifrost): same code as a kext
```

The `ib_device_ops` struct contains function pointers for every verbs
operation: query_device, alloc_pd, create_cq, create_qp, reg_mr,
post_send, post_recv, poll_cq, req_notify_cq, and 100+ more.

**Verbifrost Phase 1: port Linux mlx5_ib driver to a macOS kext that
fills ib_device_ops and calls ib_register_device.** Only macOS-specific
parts: kext packaging, IOKit PCI matching, peer discovery.

## Provider registration flow

```
1. Provider kext loads
2. Fills ib_device_ops, calls ib_register_device()
3. IORDMAFamily creates IORDMAFamilyUC UserClient
4. Sets IOKit properties: node_guid, node_type, abi_version, driver_id
5. libibverbs.dylib discovers it via ibv_get_device_list()
```

## Updated architecture (final)

```
Application (MLX jaccl, exo, or any ibv_* client)
    ↓
libibverbs.dylib → ibv_cmd_* → mach IPC
    ↓
IORDMAFamily.kext = Linux drivers/infiniband/core/ ported to macOS
    → ib_register_device, ib_alloc_device, uverbs
    → IORDMAInterface (provider abstract base class)
    ├── TB Provider (Apple) — Thunderbolt RDMA
    └── [ConnectX Provider — TO BUILD] — Verbifrost Phase 1
        → port mlx5_ib, fill ib_device_ops, call ib_register_device
```

**Phase 0 is now effectively complete.** We understand the entire stack.
