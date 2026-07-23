# DriverKit-AppleEthernetMLX5 Analysis

Findings from a live ConnectX-6 LX in a Thunderbolt enclosure on a Mac Studio
M3 Ultra (macOS 26.4, Darwin 25.4.0, T6031 chip).

> **⚠️ Major update (2026-07-23):** String analysis of the dext binary reveals
> that Apple implemented the **complete mlx5 RDMA command set** inside the
> dext — including QP, CQ, PD, MR, and RoCEv2 address management commands.
> The RDMA infrastructure is present in the binary but is not exposed via any
> userspace API. See the "Hidden RDMA capability" section below.

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

## Dext location and structure

```
Path:       /System/Library/DriverExtensions/com.apple.DriverKit-AppleEthernetMLX5.dext
Binary:     com.apple.DriverKit-AppleEthernetMLX5  (719 KB, universal: x86_64 + arm64e)
Info.plist: com.apple.DriverKit-AppleEthernetMLX5.dext/Info.plist
Bundle ID:  com.apple.DriverKit-AppleEthernetMLX5
Version:    1.0 (built against DriverKit 25.4)
```

The dext is a **flat Mach-O binary** (not a .app bundle structure).

## Dext entitlements

From `codesign -d --entitlements -`:

```
com.apple.developer.driverkit                   = true
com.apple.developer.driverkit.family.networking = true
com.apple.developer.driverkit.transport.pci     = true
```

These are the **same three entitlements** any third-party DriverKit network
dext needs. The `transport.pci` entitlement grants PCI config space and BAR
access — meaning a well-entitled third-party dext can access the same hardware.

## IOKit personality (device matching)

From `Info.plist`:

```xml
<key>IOPCIMatch</key>      <string>0x000015b3&0x0000ffff</string>
<key>IOPCIClassMatch</key> <string>0x02000000&0xffffff00</string>
<key>IOPCITunnelCompatible</key> <true/>
<key>IOProviderClass</key> <string>IOPCIDevice</string>
<key>IOUserClass</key>     <string>DriverKit_AppleEthernetMLX5</string>
```

**Coexistence:** Apple's match is broad (`0x000015b3&0x0000ffff` = any Mellanox
device). Only one dext can claim a PCI function. Verbifrost must use a narrower
match, boot with Apple's dext unloaded, or add a higher `IOProbeScore`.

## C++ class structure (from demangled symbols)

### `DriverKit_AppleEthernetMLX5` (the PCI device driver)

Methods (demangled from `nm` output):
- `Start_Impl(IOService*)` — dext entry point
- `Stop_Impl(IOService*)` — teardown
- `SetPowerState_Impl(unsigned int)` — power management
- `AsyncInterrupt_Impl` / `PagesInterrupt_Impl` / `QueueInterrupt_Impl` — event handlers
- `CommandInterrupt_Impl` — command completion from HCA
- `CmdTimerOccurred_Impl` — command timeout handler
- `HealthTimerOccurred_Impl` — health check timer

The interrupt handlers map directly to mlx5 hardware event queue types:
**Pages** = page management events, **Queue** = WQ completion events,
**Command** = HCA command completions.

### `DriverKit_AppleEthernetMLX5_NetIf` (the network interface — Ethernet only)

Pure `IOUserNetworkEthernet` overrides: `SetMTU`, `getHardwareAddress`,
`setHardwareAssists`, `GetMaxTransferUnit`, `SetInterfaceEnable`,
`SetPromiscuousModeEnable`, `getTSOOptions`, etc. **No RDMA methods.**

## Hidden RDMA capability ⭐

**The most significant Phase 0 finding.**

String analysis (`strings` on the dext binary) reveals Apple compiled in the
**complete mlx5 HCA command interface**, including RDMA verbs never used by
the Ethernet data path:

### QP state machine (full InfiniBand spec)

```
CREATE_QP    DESTROY_QP    QUERY_QP
RST2INIT_QP  INIT2INIT_QP  RTR2RTS_QP  RTS2RTS_QP
SQD_RTS_QP   SQERR2RTS_QP
```

### CQ, PD, MR, UAR, EQ, SRQ

```
CREATE_CQ / DESTROY_CQ / QUERY_CQ / MODIFY_CQ
ALLOC_PD / DEALLOC_PD
CREATE_MKEY / DESTROY_MKEY / QUERY_MKEY   (MKey = Memory Region key)
ALLOC_UAR / DEALLOC_UAR                    (UAR = User Access Region)
CREATE_EQ / DESTROY_EQ / QUERY_EQ          (EQ = Event Queue)
CREATE_SRQ / DESTROY_SRQ                   (Shared Receive Queue)
```

### RoCEv2 address management ⭐

```
SET_ROCE_ADDRESS       QUERY_ROCE_ADDRESS
QUERY_HCA_VPORT_GID    QUERY_HCA_VPORT_PKEY
```

These manage the GID table and RoCEv2 address config — exactly what's needed
to make the card speak RoCEv2 to a DGX Spark.

### HCA initialization

```
ENABLE_HCA    INIT_HCA    QUERY_HCA_CAP    SET_HCA_CAP
QUERY_ADAPTER QUERY_PAGES SET_ISSI
```

### Complete command set: 120+ opcodes

Including flow steering, congestion control, e-switch/virtualization, QoS.
See the full list in the raw research data.

## Interpretation

Apple's dext is **not a thin Ethernet driver** — it is a full mlx5 core driver
that only exposes the Ethernet datapath. The RDMA command infrastructure is
compiled in but dormant.

**Implication for Verbifrost:** This is a major shortcut. Instead of
reimplementing the mlx5 command interface from scratch, we may be able to:
1. Fork Apple's dext and add a verbs `IOUserClient` — reusing the existing
   HCA command implementation
2. Or: determine if Apple's dext already initializes the HCA into a state
   where RDMA commands can be issued, and add only the userspace API layer

## PCIe BAR mapping

```
IOPCIDeviceMemoryMapBase: 67239936  (0x04008000)
IOPCIDeviceMemoryMapSize: 131008    (~128 KB)
IOPCIDeviceMapperPageSize: 16384
```

The BAR (HCA register space) is mapped at physical 0x04008000. The dext
uses `IODMACommand::PrepareForDMA` for DMA mapping through Apple's IOMMU (DART).

