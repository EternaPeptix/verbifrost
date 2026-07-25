# Roadmap

Verbifrost is a systems project to bring RDMA/RoCEv2 to macOS for
heterogeneous AI clusters (Mac ↔ DGX Spark).

> **⚠️ Major revision (2026-07-23):** Phase 0 research discovered that macOS
> already has a complete RDMA stack (`librdma.dylib` + `IORDMAFamily.kext`).
> The project scope has been reduced from ~2 years to ~4-6 months. See
> [docs/research/librdma-discovery.md](docs/research/librdma-discovery.md).

---

## Phase 0: Research & Reconnaissance *(COMPLETE ✅)*

**Goal:** Document everything about macOS's hidden RDMA stack.

**All deliverables completed:**
- [x] Discovered `librdma.dylib` / `libibverbs.dylib` — full libibverbs in dyld cache
- [x] Verified `ibv_*` API works live (6 RDMA devices enumerated, PD/CQ creation)
- [x] Found `infiniband/verbs.h` in macOS SDK (1504 lines, standard API)
- [x] Discovered `IORDMAFamily.kext` — kernel RDMA framework
- [x] **Extracted kext binaries from kernelcache** — confirmed it's a Linux RDMA port
- [x] **Identified provider interface**: `IORDMAInterface` = Linux `ib_device_ops`
- [x] **Found `ib_register_device`** in the binary — provider registration works like Linux
- [x] Documented mlx5 dext's hidden RDMA command set
- [x] Mapped provider registration protocol (IOKit properties)
- [x] Built `vfinfo` tool — first working Verbifrost code
- [x] Traced libibverbs call chain to kernel IPC
- [x] Built kext extraction tooling (extract_kexts2.py with pyimg4)

**Key finding:** IORDMAFamily.kext is Apple's port of the Linux kernel's
`drivers/infiniband/core/`. The provider interface is the same `ib_device_ops`
struct. A ConnectX provider is just an mlx5_ib port that calls
`ib_register_device`.

---

## Phase 1: ConnectX Provider Kext *(in progress — ~70%)*

**Goal:** A kernel extension that initializes the ConnectX RDMA engine and
registers as an IORDMAFamily provider.

**Completed:**
- [x] Reverse-engineer the IORDMAFamily provider interface → 628 symbols extracted
- [x] Kext skeleton (Makefile, Info.plist, IOService subclass)
- [x] PCI device matching for ConnectX (`pci15b3,1015`, IOProbeScore=5000)
- [x] BAR0 mapping + init segment readback
- [x] mlx5 command queue (DMA ring + mailbox pool + polling execution)
- [x] ENABLE_HCA command
- [x] QUERY_PAGES command
- [x] QUERY_HCA_CAP command (parses max QPs, port count)
- [x] INIT_HCA command
- [x] Reconstruct kernel-side `ib_verbs.h` (ib_device, ib_device_ops, ib_pd/cq/qp/mr)
- [x] Reconstruct `IORDMAInterface.h` C++ class header

**Remaining:**
- [ ] **Load kext on 512S2** with SIP disabled (validate PCI + BAR0 + commands)
- [ ] Create EQ (Event Queue) for interrupt-driven completions
- [ ] Link against `com.apple.iokit.IORDMAFamily` in Info.plist
- [ ] `vfinfo` lists ConnectX as RDMA device

**Dependencies:** None (SIP must be disabled for loading)

---

## Phase 2: Verbs Implementation

**Goal:** The provider dext implements all verbs operations via IORDMAFamily.

**Deliverables:**
- [ ] Protection Domain (PD) allocation
- [ ] Memory Registration (MR) — DMA-map userspace buffers
- [ ] Completion Queue (CQ) creation and polling
- [ ] Queue Pair (QP) state machine (RST → INIT → RTR → RTS)
- [ ] Address Handle (AH) creation for RoCEv2
- [ ] Post Send / Post Recv (data path)
- [ ] End-to-end: RDMA WRITE between Mac and DGX Spark over RoCEv2

**Dependencies:** Phase 1 dext + `com.apple.private.iokit.rdma` entitlement.

---

## Phase 3: Integration

**Goal:** Real heterogeneous inference.

**Deliverables:**
- [ ] MLX distributed backend using standard verbs (replaces TCP ring)
- [ ] exo integration: Mac↔Spark RDMA pipeline parallelism
- [ ] Performance benchmarks vs TCP ring
- [ ] Multi-node testing on the real cluster

---

## Timeline (revised)

| Phase | Duration | Notes |
|-------|----------|-------|
| 0 | 1 month more | Provider interface RE is the key remaining item |
| 1 | 2-3 months | DriverKit dext + HCA init |
| 2 | 2-3 months | Verbs implementation + first RDMA transfer |
| 3 | 1-2 months | MLX/exo integration + benchmarks |
| **Total** | **~6-9 months** | (down from original ~2 years) |
