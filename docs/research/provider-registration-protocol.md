# IORDMAFamily Provider Registration Protocol

> Findings from live IOKit registry inspection of AppleThunderboltRDMA
> provider nodes on Mac Studio 512S2.

## How RDMA devices are registered

An RDMA device on macOS is an IOKit service node that:
1. Sets specific properties recognized by `IORDMAFamily.kext`
2. Exposes `IORDMAFamilyUC` as its `IOUserClientClass`
3. Is discovered by `librdma.dylib`'s `ibv_get_device_list()`

## Required IOKit properties (from live rdma_en5 node)

```
IOClass              = <provider-specific, e.g. "AppleThunderboltRDMAInterface">
IOUserClientClass    = "IORDMAFamilyUC"
node_guid            = "1330:739a:5be8:ac05"   (IB-format node GUID)
node_type            = 100                       (IORDMAFamily node type)
abi_version          = 1                         (IORDMAFamily ABI version)
driver_id            = 21                        (unique driver identifier)
```

The `node_guid` follows InfiniBand format: colon-separated hex bytes. Each
Thunderbolt bridge port gets a unique GUID derived from its MAC address.

## The IOKit service tree (Thunderbolt RDMA)

```
IOThunderboltXDomainService
  └── AppleThunderboltRDMAPeerInterface     (peer discovery via Protocol ID 0xFA57)
      └── (triggers RDMA interface creation)

IOEthernetInterface (Thunderbolt bridge port)
  └── AppleThunderboltRDMAInterface          (rdma_enN)
      ├── node_guid, node_type, abi_version  (IORDMAFamily properties)
      └── IORDMAFamilyUC                      (UserClient for librdma.dylib)
```

Two separate IOKit personalities handle the full flow:
1. **Peer discovery**: `AppleThunderboltRDMAPeerInterface` matches on
   `IOThunderboltXDomainService` with `Protocol ID = 64087` (0xFA57)
2. **Device registration**: `AppleThunderboltRDMAInterface` matches on
   `IOEthernetInterface` with `BSD Name` and `IOParentMatch = AppleThunderboltIPPort`

## Verbifrost ConnectX provider registration

For the ConnectX card, the registration will be:

```
IOPCIDevice (ConnectX-6 LX)
  └── VerbifrostConnectXRDMA                   (our DriverKit dext)
      ├── Initializes HCA: ENABLE_HCA → INIT_HCA → SET_ROCE_ADDRESS
      ├── Allocates node GUID from card's burned-in GUID
      └── Creates RDMA interface node:
          ├── IOClass = "VerbifrostConnectXRDMAInterface"
          ├── IOUserClientClass = "IORDMAFamilyUC"
          ├── node_guid = <from ConnectX GUID register>
          ├── node_type = 100
          ├── abi_version = 1
          └── driver_id = <new unique ID>
```

Once this node exists with the right properties, `librdma.dylib` will
automatically discover it via `ibv_get_device_list()`.

## What we still need to reverse-engineer

The IOKit properties tell IORDMAFamily *that* a device exists, but the
provider must also implement a C++ protocol that IORDMAFamilyUC calls into
for actual verbs operations (create QP, register MR, post send, etc.).

The question is: **what C++ interface does IORDMAFamily expect from providers?**

Likely candidates:
- An abstract base class (e.g., `IORDMADevice`) with virtual methods
- An IOKit interface (OSDynamicCast-checkable) that the provider conforms to
- A set of IOKit method invocations via `IOService::callPlatformFunction`

To determine this, we need to either:
1. Extract and disassemble `IORDMAFamily.kext` from the kernelcache
2. Trace `IORDMAFamilyUC`'s external methods to see what it calls on the provider
3. Study the `AppleThunderboltRDMA` kext binary to see what protocol it implements

## Test results: what works without special entitlements

Our `vfinfo` tool (tools/vfinfo.c) confirms:

| Operation | Works without entitlement? |
|-----------|--------------------------|
| `ibv_get_device_list` | ✅ |
| `ibv_open_device` | ✅ |
| `ibv_query_device` | ✅ |
| `ibv_query_port` | ✅ |
| `ibv_query_gid` | ✅ |
| `ibv_alloc_pd` | ✅ (on ACTIVE port only) |
| `ibv_create_cq` | ✅ (on ACTIVE port only) |
| `ibv_reg_mr` | ❌ (errno -22 — needs `com.apple.private.iokit.rdma`) |
| `ibv_dealloc_pd` | ✅ |
| `ibv_destroy_cq` | ✅ |

The `com.apple.private.iokit.rdma` entitlement (found in kernel strings) is
required for DMA operations (memory registration). This is a private
entitlement that Apple grants to its own software (jaccl/MLX). Third-party
apps will need it for full verbs functionality.
