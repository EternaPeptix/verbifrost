# Roadmap

Verbifrost is a large systems project. This document breaks it into phases
with concrete deliverables, dependencies, and success criteria.

---

## Phase 0: Research & Reconnaissance *(current)*

**Goal:** Document everything about the target environment before writing code.

**Deliverables:**
- [x] Document the 4-layer RDMA gap on macOS (see [ARCHITECTURE.md](ARCHITECTURE.md))
- [x] Reverse-engineer Apple's `DriverKit-AppleEthernetMLX5` driver — what it
      initializes, what it leaves dormant (see
      [docs/research/mlx5-driver-analysis.md](docs/research/mlx5-driver-analysis.md))
- [x] Map Apple's private `AppleThunderboltRDMAPeerInterface` — the
      undocumented RDMA path that MLX's jaccl uses
      (see [docs/research/apple-private-rdma.md](docs/research/apple-private-rdma.md))
- [ ] Map the DriverKit networking stack (`IOSkywalkFamily`, `IOUserNetworkEthernet`)
- [ ] Document the mlx5 command interface (mailbox, HCA commands, init sequence)
- [ ] Identify which DriverKit entitlements Apple requires for DMA / PCI config access
- [ ] Test: can we read mlx5 PCI config space and VPD from userspace on macOS?

**Success:** Anyone joining the project can understand the full stack from
the docs alone.

---

## Phase 1: DriverKit RDMA Transport Skeleton

**Goal:** A DriverKit dext that registers a character device and accepts
verbs ioctls — no hardware yet.

**Deliverables:**
- [ ] DriverKit dext project skeleton (Xcode, entitlements, provisioning)
- [ ] PCI matching personality for `vendor_id=0x15b3` (Mellanox)
- [ ] `/dev/verbifrost/uverbs0` character device exposed to userspace
- [ ] ioctl ABI definition for MR/QP/CQ operations
- [ ] Internal data structures for tracking objects (MR, QP, CQ, PD, AH)
- [ ] Basic test: open device, query capabilities, close

**Dependencies:** Phase 0 research on DriverKit entitlements and IOKit
service registration.

**Success:** `vfinfo` (our `ibv_devices` equivalent) lists the ConnectX card
as an RDMA device.

---

## Phase 2: ConnectX Verbs Provider

**Goal:** The dext can actually drive the mlx5 hardware — initialize the RDMA
engine, create queue pairs, register memory, and perform RDMA writes/reads.

**Deliverables:**
- [ ] mlx5 initialization sequence (HCA reset, boot, firmware load)
- [ ] Command queue (CMD_IF) implementation — mailbox-based HCA commands
- [ ] RoCEv2 firmware configuration (set port parameters, enable RoCE)
- [ ] Protection Domain (PD) allocation
- [ ] Memory Registration (MR) — DMA-map userspace buffers
- [ ] Completion Queue (CQ) creation and polling
- [ ] Queue Pair (QP): RST → INIT → RTR → RTS state machine
- [ ] Address Handle (AH) creation for RoCEv2 (GID, GID index, UDP port)
- [ ] Post Send / Post Recv verbs
- [ ] End-to-end test: RDMA WRITE between two Mac Studios over 25GbE

**Dependencies:** Phase 1 dext skeleton; physical ConnectX hardware on macOS.

**Success:** `vf_ping` achieves sub-5µs round-trip latency between two Mac
Studios connected via SFP28 DAC.

---

## Phase 3: Userspace Verbs Library

**Goal:** A drop-in `libibverbs` / `librdmacm` port that lets existing RDMA
software run on macOS.

**Deliverables:**
- [ ] `libverbifrost` — the verbs provider that calls the dext via ioctl
- [ ] Port of `libibverbs` core (context, PD, MR, CQ, QP, AH, SRQ APIs)
- [ ] Port of `librdmacm` (connection manager, RDMA CM ID, address resolution)
- [ ] RoCEv2 GID table management
- [ ] Compatible header files (`<infiniband/verbs.h>`, `<rdma/rdma_cma.h>`)
- [ ] Test: compile and run `rdma-core` example programs unmodified

**Dependencies:** Phase 2 hardware driver.

**Success:** Any application that uses `libibverbs` on Linux compiles and
runs on macOS against `libverbifrost` with no source changes.

---

## Phase 4: Integration & Real-World Use

**Goal:** Verbifrost is used for actual heterogeneous AI inference.

**Deliverables:**
- [ ] New MLX distributed backend (`verbifrost`) that uses standard verbs
- [ ] exo integration: Mac↔Spark RDMA inference over RoCEv2
- [ ] NCCL compatibility study (can Mac join an NCCL communicator?)
- [ ] Performance benchmarks: RDMA vs TCP ring on the same hardware
- [ ] Multi-node CI test with Mellanox cards

**Dependencies:** Phases 1–3 complete and stable.

**Success:** A DeepSeek-V3 model runs with pipeline parallelism across
Mac Studios and DGX Sparks using RoCEv2 RDMA for the cross-island link,
achieving >2× the throughput of the TCP ring backend.

---

## Timeline estimate

This is a multi-year effort. Realistic rough estimates:

| Phase | Duration | Parallelizable? |
|-------|----------|----------------|
| 0 | 2–3 months (ongoing) | Yes — many independent research threads |
| 1 | 3–6 months | Partially — dext skeleton can start now |
| 2 | 6–12 months | No — sequential hardware bring-up |
| 3 | 3–6 months | Yes — can overlap with Phase 2 |
| 4 | 3–6 months | Yes — multiple integration targets |

These assume 2–3 active contributors. The critical path is Phase 2 (mlx5
verbs provider), which requires deep expertise in both mlx5 firmware and
DriverKit DMA APIs.
