# Architecture

## The problem in one diagram

```
    Mac Studio (macOS)                          DGX Spark (Linux)
    ┌─────────────────┐                        ┌─────────────────┐
    │   Application   │                        │   Application   │
    │  (MLX / exo)    │                        │  (NCCL / exo)   │
    ├─────────────────┤                        ├─────────────────┤
    │   MLX jaccl     │ ◄── Apple proprietary  │   NCCL          │
    │   (TB RDMA)     │     Mac↔Mac ONLY       │   (RoCEv2)      │
    ├─────────────────┤                        ├─────────────────┤
    │  ❌ No libibverbs│                        │  libibverbs     │
    │  ❌ No verbs API │                        │  librdmacm      │
    ├─────────────────┤                        ├─────────────────┤
    │  ❌ No ib_core   │                        │  ib_core.ko     │
    │  ❌ No mlx5_ib   │                        │  mlx5_ib.ko     │
    ├─────────────────┤                        ├─────────────────┤
    │ DriverKit-MLX5  │ ◄── Ethernet ONLY      │ mlx5_core.ko    │
    │ (Ethernet path) │     RDMA dormant       │ + RDMA engine   │
    ├─────────────────┤                        ├─────────────────┤
    │ ConnectX-6 LX   │                        │ ConnectX-7      │
    │ (25GbE SFP28)   │                        │ (200GbE QSFP56) │
    └────────┬────────┘                        └────────┬────────┘
             │                                          │
             └────────── Ethernet / MikroTik ───────────┘
                        (TCP only, no RDMA)
```

Both sides have Mellanox hardware. Both chips support RoCEv2. But the macOS
side never initializes the RDMA engine, and there's no verbs API to call
even if it did.

## What Verbifrost adds

```
    Mac Studio (macOS with Verbifrost)
    ┌─────────────────┐
    │   Application   │
    │  (MLX / exo)    │
    ├─────────────────┤
    │  MLX verbifrost │ ◄── NEW backend (Phase 4)
    │   backend       │
    ├─────────────────┤
    │ libverbifrost   │ ◄── NEW (Phase 3) — drop-in ibv_* API
    ├─────────────────┤
    │ verbifrost dext │ ◄── NEW (Phase 1) — RDMA transport
    │  (ib_core +     │      + mlx5 verbs provider (Phase 2)
    │   mlx5_ib)      │
    ├─────────────────┤
    │ ConnectX-6 LX   │
    │ RDMA engine ON  │ ◄── firmware initialized for RoCEv2
    └────────┬────────┘
             │
             └────────── RoCEv2 RDMA ──────────► DGX Spark
```

## Component design

### 1. DriverKit RDMA dext (`kernel/`)

The core kernel-side component. In Linux this is split across `ib_core.ko`
(generic RDMA subsystem) and `mlx5_ib.ko` (Mellanox-specific). We combine
both into a single DriverKit dext for simplicity.

**Responsibilities:**

| Subsystem | Linux equivalent | What it does |
|-----------|-----------------|-------------|
| Device matching | PCI match table | Match `vendor=0x15b3, class=0x020000` (Mellanox NIC) |
| HCA init | `mlx5_init_one()` | Reset chip, load firmware, enable RoCEv2 |
| Command interface | `mlx5_cmd_*()` | Mailbox commands to the NIC's embedded CPU |
| Uverbs | `/dev/infiniband/uverbsN` | ioctl ABI for userspace verbs calls |
| Event dispatch | `ib_dispatch_event()` | CQ completion, QP state change, port events |
| DMA mapping | `ib_dma_*()` | Map userspace buffers for NIC DMA access |

**Key challenge:** DriverKit runs in **userspace**, not the kernel. DMA
mapping must go through Apple's IOMMU (DART on Apple Silicon). We need the
`com.apple.developer.driverkit.transport.pci` entitlement to access PCI
config space, and `IODMACommand` for buffer mapping.

### 2. ConnectX verbs provider (part of the dext)

This is the `mlx5_ib.ko` equivalent — code that talks to Mellanox chips.

**Critical sequence — RoCEv2 initialization:**

```
1.  PCI config space access → read device capabilities
2.  Map HCA BAR (PCIe memory-mapped registers)
3.  Reset HCA      (MLX5_CMD_OP_ENABLE_HCA)
4.  Query HCA caps (limits, RoCE support, port count)
5.  Set RoCEv2 source UDP port (4791)
6.  Allocate protection domains, create completion queues
7.  Initialize UAR (User Access Region) for doorbell writes
8.  Create EQ (Event Queue) for completion events
9.  Set port parameters (speed, RoCE mode, GID table)
```

Steps 1–9 are documented in the mlx5 core driver source
(`drivers/net/ethernet/mellanox/mlx5/core/` in the Linux kernel). We
reimplement them against DriverKit's PCI and DMA APIs.

### 3. Userspace verbs library (`userspace/`)

A port of `rdma-core`'s `libibverbs` and `librdmacm`. This is the API layer
that applications call:

```c
// Standard ibv_* API — same on Linux and macOS with Verbifrost
struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
struct ibv_context *ctx = ibv_open_device(dev_list[0]);
struct ibv_pd *pd = ibv_alloc_pd(ctx);
struct ibv_mr *mr = ibv_reg_mr(pd, buf, len, IBV_ACCESS_LOCAL_WRITE);
struct ibv_cq *cq = ibv_create_cq(ctx, 128, NULL, NULL, 0);
// ... create QP, post send/recv, poll completions
```

Internally, each `ibv_*` call translates to an ioctl on
`/dev/verbifrost/uverbs0`. The ioctl ABI mirrors Linux's uverbs but uses
Darwin-native structures (no netlink, no udev).

### 4. MLX distributed backend (Phase 4)

A new MLX backend that calls verbs directly, bridging the gap between
`jaccl` (Apple TB-only) and `ring` (TCP). This backend would:

- Use `ibv_post_send` / `ibv_post_recv` for point-to-point transfers
- Use `ibv_send_wr` with `IBV_WR_RDMA_WRITE` for zero-copy sends
- Work on any platform with `libibverbs` (macOS via Verbifrost, Linux natively)
- Enable true Mac↔Spark RDMA for heterogeneous inference

## Alternatives considered

### Port Apple's private jaccl API

Apple's `AppleThunderboltRDMAPeerInterface` is a private RDMA implementation.
We could reverse-engineer and expose it publicly. **Why not:** Thunderbolt-only
(no Ethernet/RoCE path), undocumented (no headers), tightly coupled to Apple's
IOKit service graph. Wouldn't interoperate with RoCEv2 on Linux.

### Software RDMA (SoftRoCE / rxe)

Linux's `rdma_rxe` is a software RDMA over any Ethernet interface using UDP.
**Why not:** Gives the verbs *API* but not performance — still goes through
TCP/IP stack, no faster than exo's existing ring backend. The point of
Verbifrost is the hardware RDMA engine for sub-microsecond latency.

### Run Linux on Mac (Asahi)

Asahi Linux gives full RDMA support immediately. **Why not (as a project):**
Requires abandoning macOS, losing Metal/MLX, running on an immature GPU stack.
Verbifrost keeps the macOS experience intact. (Valid option for individuals.)

