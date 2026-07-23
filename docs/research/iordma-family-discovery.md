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

## Implications for Verbifrost (Mac↔Spark RDMA)

> **Goal reminder:** Mac↔Mac RDMA already works via Apple's jaccl backend.
> Verbifrost exists to enable **Mac↔DGX Spark RDMA over Ethernet/RoCEv2**.

The IORDMAFamily discovery is relevant to our goal **only if** the framework
is transport-agnostic — i.e., if we can write a provider that registers a
ConnectX/RoCEv2 device, and the existing `IORDMAFamilyUC` would then give
us verbs that produce standard RoCEv2 packets interoperable with Linux.

### Path A: Fork Apple's mlx5 dext (most direct path to Mac↔Spark)

Write a new DriverKit dext that claims the ConnectX card, issues the
`SET_ROCE_ADDRESS` / `CREATE_QP` / `CREATE_CQ` commands (which Apple's dext
already contains) to initialize RoCEv2, and exposes verbs via our own
UserClient. The RoCEv2 packets flow over 25GbE Ethernet to the Spark's
ConnectX-7 — fully interoperable because both speak standard RoCEv2.

**This is the surest path to Mac↔Spark.** It doesn't depend on IORDMAFamily.

### Path B: Write an IORDMAFamily provider for ConnectX (shortcut, IF possible)

If `IORDMAFamily` is transport-agnostic (not Thunderbolt-coupled at the verbs
layer), we write a provider dext that registers the ConnectX card. The
existing `IORDMAFamilyUC` would then provide RoCEv2 verbs. Less code than
Path A, but depends on an unknown: does IORDMAFamily accept non-TB providers?

### Path C: Call IORDMAFamilyUC from userspace (Mac↔Mac only — NOT our goal)

`IORDMAFamilyUC` is attached to Thunderbolt RDMA interfaces. Calling it
directly gives Mac↔Mac RDMA — **which jaccl already does**. This path does
not serve the Mac↔Spark goal and is listed only for completeness.

## Next steps

- [ ] Find the `IORDMAFamily` binary (kernelcache extraction)
- [ ] Dump `IORDMAFamilyUC`'s external method table (selectors)
- [ ] Attempt to open `IORDMAFamilyUC` from a test userspace program
- [ ] Trace `IOConnectCallMethod` calls from MLX's jaccl to learn the API
- [ ] Determine if `IORDMAFamily` is transport-agnostic (PCIe/Mellanox
      compatible) or Thunderbolt-specific

