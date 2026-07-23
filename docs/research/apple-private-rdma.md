# Apple's Private RDMA Interface

Apple ships a private, undocumented RDMA implementation used exclusively by
MLX's `jaccl` distributed backend. This document records what we know from
IOKit registry inspection.

## The class: AppleThunderboltRDMAPeerInterface

Found in the IOKit service tree on both Mac Studios:

```
Class: AppleThunderboltRDMAPeerInterface
Instance count: 1 (per node)
```

This class is loaded by `com.apple.driver.AppleThunderboltNHI` — the
Thunderbolt NHI (Native Host Interface) driver. It is **not** exported in
any public framework or SDK.

## What it does (inferred)

Based on the class name and its parent in the IOKit service graph:

1. **Exposes an RDMA-like API** to privileged callers (MLX's jaccl)
2. **Operates over Thunderbolt only** — it's a child of the TB NHI driver,
   not the Ethernet or PCI networking stack
3. **Creates virtual RDMA interfaces** named `rdma_en<N>` — these map to
   Thunderbolt bridge ports, not to physical NIC ports
4. **Handles peer-to-peer DMA** between Thunderbolt-connected Macs without
   going through the TCP/IP stack

## The jaccl backend (MLX distributed)

MLX's `jaccl` backend is the only known consumer of this API. From the MLX
documentation and exo's source code:

```
Environment variables for jaccl:
  MLX_IBV_DEVICES     → JSON file with interface name matrix
  MLX_JACCL_COORDINATOR → IP:port of rank 0
  MLX_RANK             → this rank's ID

The MLX_IBV_DEVICES matrix uses "rdma_enN" interface names:
  [
    [null,    "rdma_en5", "rdma_en4"],
    ["rdma_en5", null,    "rdma_en3"],
    ["rdma_en4", "rdma_en3", null   ]
  ]
```

The naming convention `rdma_en<N>` mirrors the Thunderbolt bridge
interfaces (`en2`–`en7` on Mac Studio). These are **not** the Mellanox
card's interfaces.

## Why we can't reuse it

| Limitation | Impact |
|-----------|--------|
| **No public headers** | Can't call the API from third-party code |
| **No SDK documentation** | Don't know the method signatures |
| **Thunderbolt-only** | No code path for Ethernet/RoCE — can't reach DGX Sparks |
| **Apple-proprietary wire protocol** | Packets don't interoperate with RoCEv2 |
| **Tightly coupled to NHI driver** | Can't redirect to a different transport |

Even if we reverse-engineered the API, it wouldn't help with the core goal
(RoCEv2 interop with Linux/NVIDIA systems). It's a dead end for heterogeneous
clusters.

## What we learned from it

The existence of `AppleThunderboltRDMAPeerInterface` proves that:

1. **Apple has an internal RDMA framework** — they just don't expose it
2. **macOS kernels can do RDMA** — the infrastructure exists at the IOKit level
3. **Peer-to-peer DMA between Macs works** — jaccl achieves ~5 GB/s with
   sub-microsecond latency over Thunderbolt

This validates the Verbifrost approach: if Apple can build an RDMA transport
as a private IOKit service, we can build one as a public DriverKit dext.

## Reverse engineering notes (TODO)

- [ ] Dump the `AppleThunderboltNHI` kext's symbol table to find the
      `AppleThunderboltRDMAPeerInterface` vtable
- [ ] Use `lldb` with kernel debugging to trace jaccl's calls into the API
- [ ] Check if the class has any selectors visible via `otool -ov` on the kext
- [ ] Determine the data path: does it use Thunderbolt PCIe tunneling for
      DMA, or Thunderbolt Networking (IP-over-TB)?
