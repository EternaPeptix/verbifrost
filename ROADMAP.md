# Roadmap

Verbifrost is a systems project to bring RDMA/RoCEv2 to macOS for
heterogeneous AI clusters (Mac ↔ DGX Spark).

> **⚠️ Major revision (2026-07-23):** Phase 0 research discovered that macOS
> already has a complete RDMA stack (`librdma.dylib` + `IORDMAFamily.kext`).
> The project scope has been reduced from ~2 years to ~4-6 months. See
> [docs/research/librdma-discovery.md](docs/research/librdma-discovery.md).

---

## Phase 0: Research & Reconnaissance *(current — nearly complete)*

**Goal:** Document everything about macOS's hidden RDMA stack.

**Completed:**
- [x] Discovered `librdma.dylib` — macOS's hidden `libibverbs` (dyld shared cache)
- [x] Verified `ibv_*` API works live (6 RDMA devices enumerated)
- [x] Found `infiniband/verbs.h` in macOS SDK (1504 lines, standard API)
- [x] Discovered `IORDMAFamily.kext` — kernel RDMA framework
- [x] Discovered `AppleThunderboltRDMA.kext` — Thunderbolt RDMA provider
- [x] Documented mlx5 dext's hidden RDMA command set (120+ HCA commands)
- [x] Mapped provider registration protocol (IOKit properties)
- [x] Built `vfinfo` tool — first working Verbifrost code

**Remaining:**
- [ ] Extract and disassemble `IORDMAFamily.kext` to find provider C++ interface
- [ ] Determine what virtual methods `IORDMAFamilyUC` dispatches to providers
- [ ] Investigate `com.apple.private.iokit.rdma` entitlement requirements
- [ ] Study how `AppleThunderboltRDMA` implements the provider interface

---

## Phase 1: ConnectX Provider Dext

**Goal:** A DriverKit dext that initializes the ConnectX RDMA engine and
registers as an IORDMAFamily provider.

**Deliverables:**
- [ ] Reverse-engineer the IORDMAFamily provider C++ interface
- [ ] DriverKit dext skeleton (Xcode project, entitlements, provisioning)
- [ ] PCI device matching for ConnectX (vendor 0x15b3)
- [ ] HCA initialization: `ENABLE_HCA` → `INIT_HCA` → `SET_ROCE_ADDRESS`
- [ ] Create IOKit service node with IORDMAFamily properties
- [ ] `vfinfo` lists ConnectX as an RDMA device

**Dependencies:** Phase 0 provider interface RE.

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
