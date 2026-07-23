# Apple's Private RDMA Framework: IORDMAFamily

> **⚠️ Discovery (2026-07-23):** macOS has a **private kernel RDMA framework**
> called `IORDMAFamily` with a userspace UserClient (`IORDMAFamilyUC`).
> This is not Thunderbolt-specific — it is a general RDMA transport layer
> that Apple built but never documented or exposed publicly.

## Discovery method

While inspecting the `AppleThunderboltRDMA.kext` Info.plist, we found it
depends on a previously unknown IOKit framework:

```xml
<key>OSBundleLibraries</key>
<dict>
    <key>com.apple.iokit.IORDMAFamily</key>
    <string>1.0</string>
</dict>
```

Searching the IOKit service registry confirmed active instances:

```
"IOUserClientClass" = "IORDMAFamilyUC"
```

This appears on multiple `AppleThunderboltRDMAInterface` nodes (rdma_en2,
rdma_en3, rdma_en5) — one per Thunderbolt bridge port.

## What IORDMAFamily provides

`IORDMAFamily` is Apple's equivalent of Linux's `ib_core` — the generic RDMA
subsystem between hardware drivers and userspace:

| Component | Linux equivalent | Role |
|-----------|-----------------|------|
| `IORDMAFamily` (kernel framework) | `ib_core.ko` | RDMA object management (QP, CQ, MR, PD, AH) |
| `IORDMAFamilyUC` (UserClient) | `/dev/infiniband/uverbsN` | Userspace verbs access via IOConnect |
| `AppleThunderboltRDMAInterface` | `ib_device` provider | Hardware-specific RDMA provider (TB) |

The UserClient class (`IORDMAFamilyUC`) is the critical piece — Apple already
has a userspace verbs API. It's undocumented, not in any SDK header, and
currently only attached to Thunderbolt RDMA interfaces.

## The kext stack

```
/System/Library/Extensions/
├── AppleThunderboltRDMA.kext          ← TB-specific RDMA driver
│   ├── AppleThunderboltRDMAInterface  (creates rdma_enN interfaces)
│   └── AppleThunderboltRDMAPeerInterface (peer discovery)
├── IOThunderboltFamily.kext           ← TB transport layer
└── [IORDMAFamily]                     ← RDMA core (private, location TBD)
```

`IORDMAFamily` is not a standalone `.kext` on disk. It is likely linked into
the kernelcache (prelinked kext) or bundled inside a larger kext.

## AppleThunderboltRDMA.kext details

### Protocol ID 64087 (0xFA57)

The Thunderbolt XDomain service protocol ID `0xFA57` identifies Apple's RDMA
protocol over Thunderbolt. This is how Thunderbolt-connected Macs discover
each other's RDMA capability.

### Dependencies

```
com.apple.iokit.IORDMAFamily          1.0  ← THE RDMA FRAMEWORK
com.apple.iokit.IOThunderboltFamily   9.3.3
com.apple.iokit.IONetworkingFamily    3.4
com.apple.iokit.IOPCIFamily           2.0
```

## Implications for Verbifrost

This discovery fundamentally changes the project strategy — two new viable
paths exist:

### Path A: Fork Apple's mlx5 dext (original plan)

Write a new DriverKit dext that claims the Mellanox PCI device and exposes
RDMA via a new `IOUserClient`. Reuses the mlx5 command code Apple compiled
into their dext.

### Path B: Use IORDMAFamily directly ⭐ NEW

Since `IORDMAFamily` already provides `IORDMAFamilyUC`, we may write an
`IORDMAFamily` *provider* dext for the ConnectX card (like
`AppleThunderboltRDMA` does for Thunderbolt). The existing `IORDMAFamilyUC`
would then provide verbs access. Much less work than Path A — we'd implement
a hardware adapter, not the entire RDMA stack.

**However:** `IORDMAFamily` is private. We'd need to reverse-engineer its
C++ ABI and determine if it's transport-agnostic.

### Path C: Call IORDMAFamilyUC from userspace ⭐ NEW

`IORDMAFamilyUC` is already attached to live Thunderbolt RDMA interfaces. We
can call it directly via `IOServiceOpen` + `IOConnectCallMethod`:

```c
io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault,
    IOServiceMatching("AppleThunderboltRDMAInterface"));
io_connect_t conn;
IOServiceOpen(service, mach_task_self(), 0, &conn);
IOConnectCallMethod(conn, selector, ...);  // verbs calls!
```

This gives immediate RDMA access over Thunderbolt (Mac↔Mac) without kernel
code. Then extend the pattern to Mellanox hardware via a provider dext.

## Next steps

- [ ] Find the `IORDMAFamily` binary (kernelcache extraction)
- [ ] Dump `IORDMAFamilyUC`'s external method table (selectors)
- [ ] Attempt to open `IORDMAFamilyUC` from a test userspace program
- [ ] Trace `IOConnectCallMethod` calls from MLX's jaccl to learn the API
- [ ] Determine if `IORDMAFamily` is transport-agnostic (PCIe/Mellanox
      compatible) or Thunderbolt-specific

