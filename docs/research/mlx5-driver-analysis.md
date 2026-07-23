# DriverKit-AppleEthernetMLX5 Analysis

Findings from a live ConnectX-6 LX in a Thunderbolt enclosure on a Mac Studio
M3 Ultra (macOS 15.4, Darwin 25.4.0, T6031 chip).

## Hardware identification

```
PCI Vendor ID:     0x15b3  (Mellanox Technologies)
PCI Device ID:     0x1015  (ConnectX-6 LX, dual-port SFP28)
Subsystem Vendor:  0x15b3
Subsystem ID:      0x0021
PCI Class:         0x020000  (Network controller → Ethernet controller)
Compatible string: "pci15b3,21","pci15b3,1015","pciclass,020000"
```

**Card model:** ConnectX-6 LX MCX631102AS (dual SFP28, 25GbE per port).
Identified by device ID 0x1015 and subsystem 0x0021.

## Apple's driver

macOS ships a first-party DriverKit driver extension (dext) for this card:

```
IOClass:         IOUserService
IOUserClass:     DriverKit_AppleEthernetMLX5
IOUserServerName: com.apple.DriverKit-AppleEthernetMLX5
IOModel:         mlx5
IOVendor:        Mellanox
IOPCIMatch:      0x000015b3&0x0000ffff  (matches any Mellanox PCI device)
IOPCIClassMatch: 0x02000000&0xffffff00  (network controller class)
IOPCITunnelCompatible: Yes               (works through Thunderbolt)
```

Source: `ioreg -l -r -c IOPCIDevice` output.

## What the driver initializes

The dext provides **Ethernet functionality only**:

| Capability | Status | Evidence |
|-----------|--------|---------|
| Ethernet link (25GBase-CR) | ✅ Active | `ifconfig en17` shows `25GBase-CR <full-duplex,flow-control>` |
| TCP/IP stack | ✅ Works | IP 192.168.0.3, routes through switch |
| Frame sizes up to 9000 (jumbo) | ✅ Configurable | MTU field |
| RoCEv2 RDMA engine | ❌ **Never initialized** | No GID table, no QP support, no firmware RoCE mode |
| InfiniBand verbs | ❌ **No uverbs device** | No `/dev/infiniband/` entries |
| Memory registration (MR) | ❌ | No DMA registration API exposed |
| Queue pairs (QP) | ❌ | No RDMA transport objects |

The dext creates `IOSkywalkLegacyEthernet` → `IOUserNetworkEthernet` interface
objects (en16, en17). These are pure Ethernet — no RDMA transport is
registered in the IOKit service graph.

## PCIe link topology

The card is tunneled through Thunderbolt:

```
Slot: Thunderbolt@3,0,0  (function 0 — port 1)
Slot: Thunderbolt@3,0,1  (function 1 — port 2)
Link Width: x4
Link Speed: 8.0 GT/s  (PCIe Gen 3)
Link Status: Link up
IOPCITunnelled: Yes
IOPCITunnelRootDeviceVendorID: 0x8086  (Intel — TB controller)
```

PCIe Gen3 x4 through Thunderbolt = **31.5 Gbps** tunnel bandwidth. This
caps the card at ~3.9 GB/s of PCIe throughput regardless of the Ethernet
link speed. The 25GbE Ethernet link (3.1 GB/s) fits within this tunnel.

**Implication:** Even with RDMA enabled, the Thunderbolt tunnel limits
single-port throughput. A TB5 enclosure (PCIe Gen4 x4) would double this.

## MAC address

```
Port 1: 50:6b:4b:xx:xx:xx  (Mellanox OUI)
Port 2: 50:6b:4b:xx:xx:xy  (sequential)
```

OUI `50:6b:4b` is registered to Mellanox Technologies — confirms genuine
Mellanox hardware, not a rebranded chip.

## What we need to access for RDMA

To initialize the RDMA engine, we need to perform these operations that
Apple's driver does **not** do:

1. **PCI BAR mapping** — map the card's memory-mapped registers. Apple's
   dext maps these internally but doesn't expose the mapping.
2. **HCA command interface** — send mailbox commands (ENABLE_HCA,
   QUERY_HCA_CAP, etc.) via the card's command queue.
3. **Firmware mode set** — tell the NIC firmware to enable RoCEv2 mode
   (currently it's in Ethernet-only mode).
4. **DMA registration** — map userspace memory buffers for the NIC to
   access via DMA. Requires `IODMACommand` with the IOMMU.

All four require PCI config space and BAR access, which needs the
`com.apple.developer.driverkit.transport.pci` entitlement — a privileged
DriverKit entitlement that Apple grants selectively.

## Open questions

- [ ] Can a third-party dext coexist with Apple's
      `DriverKit-AppleEthernetMLX5`? Or will Apple's dext claim the device
      first?
- [ ] Does Apple's dext hold the PCI device exclusively, preventing a
      second dext from accessing the BAR?
- [ ] Can we use `mstflid` / `mlxconfig` from userspace to check RoCEv2
      firmware capability without a driver?
- [ ] What DriverKit entitlements does Apple's dext use? (Check its
      entitlement plist.)
