# Target Cluster Topology

This documents the real heterogeneous cluster that motivates Verbifrost.
All hardware findings were gathered from live systems via SSH.

## Overview

| Node | Type | SoC | RAM | Disk | Networking |
|------|------|-----|-----|------|-----------|
| **512S1** | Mac Studio (M3 Ultra) | T6031 | 512 GB | 926 GB SSD | CX-6 LX 25GbE + TB bridge |
| **512S2** | Mac Studio (M3 Ultra) | T6031 | 512 GB | 3.6 TB SSD | CX-6 LX 25GbE + TB bridge |
| **Sparks ×5** | DGX Spark (GB10) | Grace Blackwell | 128 GB each | varies | CX-7 200GbE + 10GbE RJ45 |

**Total RAM:** ~1.625 TB
**Total disk (model-capable):** ~5 TB+ across SSDs

## Mac Studio networking detail

Both Mac Studios have identical networking configurations:

### Mellanox ConnectX-6 LX (Thunderbolt enclosure)

```
Card:          ConnectX-6 LX (device 0x1015, vendor 0x15b3)
Driver:        com.apple.DriverKit-AppleEthernetMLX5 (Apple first-party)
Enclosure:     Thunderbolt PCIe expansion chassis
TB tunnel:     PCIe Gen3 x4 (31.5 Gbps ceiling)
Ports:         2× SFP28 (25GbE per port)
MAC OUI:       50:6b:4b (Mellanox)
```

Per-node interface mapping:

| Node | Port 1 | Port 2 | IP (active) | Link |
|------|--------|--------|-------------|------|
| 512S1 | en14 | en16 | 192.168.0.2 (en14) | 25GBase-CR |
| 512S2 | en16 | en17 | 192.168.0.3 (en17) | 25GBase-CR |

**Unused capacity:** Each Mac has one inactive SFP28 port that could be
connected for a bonded 50GbE link if the switch has spare SFP28 ports.

### Thunderbolt bridge (Mac↔Mac RDMA)

```
TB Bus 3 (Receptacle 4): 80 Gb/s — 512S1 ↔ 512S2 direct connection
                           (used by MLX jaccl for RDMA)
```

This is the only RDMA-capable link on the Macs today — Apple's proprietary
jaccl protocol over Thunderbolt.

### Wi-Fi / Tailscale

Both Macs also have Wi-Fi and Tailscale (utun100) for management access,
but these are not used for inference traffic.

## DGX Spark networking (planned)

```
ConnectX-7:    2× QSFP56 (200GbE per port)
10GbE RJ45:    Grace SoC built-in Ethernet
USB4:          2× USB-C (20 Gbps, not Thunderbolt, not RDMA)
```

Spark↔Spark traffic will use the ConnectX-7 QSFP56 ports via the MikroTik
CRS812 switch at 200GbE RoCEv2.

## Switch: MikroTik CRS812

```
Planned ports:
  - 8× SFP28 (25GbE) for Mac Studios
  - 2× QSFP56 (200GbE) for DGX Sparks (future)
```

Currently the Mac Studios connect via SFP28 DAC cables at 25GbE.

## The inter-island bottleneck

```
Mac Studio ─── 25GbE TCP ───→ MikroTik CRS812 ←── 200GbE RoCEv2 ──→ DGX Spark
             (3.1 GB/s max)                    (25 GB/s, Sparks only)
```

Mac↔Spark traffic is **TCP only** at 3.1 GB/s. This is the bottleneck that
Verbifrost aims to eliminate by enabling RoCEv2 RDMA on the Mac side.

## Bandwidth context

| Metric | Mac (TB RDMA) | Mac (CX-6 TCP) | Spark (CX-7 RDMA) | M3 Ultra memory |
|--------|-------------|---------------|-------------------|-----------------|
| Per-direction | 5 GB/s | 3.1 GB/s | 25 GB/s | 819 GB/s |
| Latency | ~1 µs | ~10 µs | ~2 µs | — |
| Bottleneck ratio | 1/164 | 1/264 | 1/33 | — (baseline) |

The network is 33–264× slower than memory. Every GB/s of network bandwidth
directly improves inference throughput for distributed models.
