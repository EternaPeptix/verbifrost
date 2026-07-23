# Verbifrost

**InfiniBand Verbs for Darwin — bridging RDMA to macOS.**

<p align="center">
  <strong>The rainbow bridge between the RDMA verbs world and Apple's OS.</strong>
</p>

---

## Why this exists

macOS has **zero RDMA support**. No `/dev/infiniband/`, no `libibverbs`, no
RoCEv2, no kernel RDMA subsystem. Every other major OS — Linux, Windows,
FreeBSD — has a verbs stack. macOS is the only holdout.

This matters for **heterogeneous AI clusters**. If you run inference across a
mix of Mac Studios and NVIDIA DGX systems, you have two RDMA islands that
can't talk to each other:

- **Macs** use Apple's proprietary Thunderbolt RDMA (`jaccl`) — fast, but
  Apple-only, undocumented, Mac↔Mac only
- **NVIDIA/Linux systems** use standard RoCEv2 (`mlx5_ib` + `libibverbs`) —
  the universal RDMA protocol

Both islands may have Mellanox ConnectX NICs speaking the *same wire
protocol*, but macOS can't access the RDMA path. Cross-island traffic falls
back to slow TCP sockets.

Verbifrost aims to close this gap by bringing the standard InfiniBand verbs
API to Darwin, so Mellanox cards on macOS can do RoCEv2 RDMA directly to any
other RDMA-capable system.

## The four-layer gap

| Layer | Linux | macOS today | Verbifrost |
|-------|-------|-------------|------------|
| **Kernel RDMA subsystem** | `ib_core.ko` | ❌ Does not exist | DriverKit dext providing RDMA transport |
| **Hardware verbs driver** | `mlx5_ib.ko` | ❌ Apple's `DriverKit-AppleEthernetMLX5` only does Ethernet | ConnectX verbs provider |
| **Userspace verbs library** | `libibverbs` / `librdmacm` | ❌ No port exists | `libverbifrost` — drop-in ibv_* API |
| **Network protocol** | RoCEv2 in NIC firmware | ❌ RDMA engine never initialized | RoCEv2 firmware init + queue pair management |

See [ARCHITECTURE.md](ARCHITECTURE.md) for the full design and
[ROADMAP.md](ROADMAP.md) for the phased delivery plan.

## Status

**Phase 0 — Research & Reconnaissance** (in progress, major findings)

No code yet, but research has uncovered two critical discoveries that
significantly reduce the project's scope:

1. **Apple's mlx5 dext contains the complete RDMA command set** — all QP,
   CQ, PD, MR, and RoCEv2 address management commands are compiled into
   `DriverKit-AppleEthernetMLX5.dext` but never exposed. See
   [docs/research/mlx5-driver-analysis.md](docs/research/mlx5-driver-analysis.md).

2. **Apple has a private RDMA framework (`IORDMAFamily`)** with a userspace
   UserClient (`IORDMAFamilyUC`). This is Apple's equivalent of Linux's
   `ib_core` + uverbs. It's undocumented but already active on Thunderbolt
   RDMA interfaces. See
   [docs/research/iordma-family-discovery.md](docs/research/iordma-family-discovery.md).

These findings mean we may not need to build the entire RDMA stack from
scratch — Apple has already built most of it. Our task may be to *expose*
what Apple has hidden, rather than *reimplement* it.

## Who we need

This is a large systems project. We need:

- **DriverKit / IOKit kernel developers** — to build the RDMA transport dext
- **mlx5 firmware experts** — to write the ConnectX verbs provider (mailbox
  commands, queue pairs, RoCEv2 init)
- **rdma-core contributors** — to port `libibverbs` to Darwin
- **macOS security researchers** — to understand Apple's private
  `AppleThunderboltRDMAPeerInterface` and whether its concepts can be reused
- **Testers with Mellanox hardware on macOS** — Thunderbolt PCIe enclosures
  with ConnectX-5/6/7 cards

If you have expertise in any of these areas, open an issue or reach out.

## Related projects

- [linux-rdma/rdma-core](https://github.com/linux-rdma/rdma-core) — the
  canonical upstream RDMA userspace libraries we aim to port
- [exo-explore/exo](https://github.com/exo-explore/exo) — distributed AI
  inference framework that would benefit from cross-platform RDMA
- [AsahiLinux/linux](https://github.com/AsahiLinux/linux) — full Linux on
  Apple Silicon (includes RDMA support, but requires abandoning macOS)

## License

Dual-licensed to match `rdma-core` upstream:
- **GPL-2.0** for kernel/DriverKit components
- **BSD-2-Clause** for userspace libraries

See [LICENSE](LICENSE) and [LICENSE.BSD](LICENSE.BSD).
