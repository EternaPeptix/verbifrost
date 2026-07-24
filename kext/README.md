# VerbifrostConnectX Kext — Phase 1

A macOS kernel extension that matches the ConnectX-6 LX PCI device
and initializes the mlx5 HCA for RDMA.

## Current status

**Phase 1a (in progress):** PCI matching + BAR0 mapping + init segment readback

The kext:
- ✅ Matches ConnectX-6 LX (`pci15b3,1015`) via IOKit personality
- ✅ Maps BAR0 (32MB at physical 0x2E5A0000000)
- ✅ Reads firmware version from init segment
- ✅ Waits for firmware boot completion
- ✅ Allocates 64KB DMA boot page
- ✅ Verifies command queue accessibility
- ⏳ ENABLE_HCA command execution (needs DMA mailbox ring)
- ⏳ INIT_HCA command execution
- ⏳ IORDMAFamily registration (Phase 2)

## Build requirements

**Full Xcode is required** (not just Command Line Tools) because kext
building needs the complete `Kernel.framework` headers.

```bash
# Install Xcode from App Store, then:
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer

# Build:
cd kext && make
```

Alternatively, open `VerbifrostConnectX.xcodeproj` in Xcode and build
with ⌘B.

## Loading (requires SIP disabled)

```bash
# Disable SIP (recovery mode):
#   csrutil disable
# Then reboot.

# Load the kext:
sudo kextload -v build/VerbifrostConnectX.kext

# Check logs:
log show --predicate 'senderImagePath CONTAINS "VerbifrostConnectX"' --last 5m

# Unload:
sudo kextunload -v -b com.verbifrost.connectx
```

## Conflict with Apple's dext

Apple's `DriverKit-AppleEthernetMLX5` dext has already claimed the
ConnectX PCI device (IOProbeScore=1000). Our kext uses IOProbeScore=5000
to take priority, but the dext may still be running.

To prevent conflict:
1. Remove the dext: `sudo rm -rf /Library/DriverExtensions/com.apple.DriverKit-AppleEthernetMLX5.systemextension`
2. Or disable SystemExtensions: boot arg `nw.system_extensions=0`

## Source structure

```
kext/
├── Makefile                         — Build without Xcode IDE
├── VerbifrostConnectX.kext/
│   └── Contents/
│       └── Info.plist               — Kext metadata + PCI matching
├── VerbifrostConnectXRDMA.cpp       — Main kext code (IOService subclass)
├── mlx5_registers.h                 — Init segment + register offsets
└── mlx5_cmd.h                       — Command queue interface
```

## mlx5 initialization sequence

```
1. mapBAR0()                    → Memory-map the 32MB PCI BAR
2. readInitSegment()            → Read FW version, cmdq params
3. waitForInitializing()        → Poll until FW boot completes
4. requestBootPages()           → Alloc 64KB DMA, trigger boot
5. verifyCmdInterface()         → Check cmdq ring in BAR0
6. sendEnableHCA()  [pending]   → Activate the HCA (opcode 0x104)
7. queryHcaCap()    [pending]   → Query device capabilities
8. initHca()        [pending]   → Initialize HCA resources
9. registerRDMA()   [Phase 2]   → ib_register_device()
```
