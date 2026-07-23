# DriverKit RDMA Transport Design

## Overview

The Verbifrost DriverKit dext (`com.verbifrost.driver.RDMATransport`) is a
userspace driver extension that provides the RDMA transport layer on macOS.

It is the Darwin equivalent of:
- `ib_core.ko` (Linux RDMA core subsystem)
- `mlx5_ib.ko` (Linux Mellanox RDMA driver)

Combined into a single dext for deployment simplicity.

## DriverKit constraints

Unlike Linux kernel modules, DriverKit dexts run in **userspace**:

| Aspect | Linux kext | DriverKit dext |
|--------|-----------|----------------|
| Address space | Kernel space | Userspace process |
| DMA access | Direct (kernel DMA API) | Via `IODMACommand` + IOMMU |
| PCI config space | Direct | Via `IOPCIDevice` service |
| Interrupts | Direct ISR | Via `IOInterruptDispatchSource` |
| Memory mapping | `vmalloc` / `kmalloc` | `IOBufferMemoryDescriptor` |
| Entitlements | None (root) | Apple-issued entitlements required |

**Required entitlements:**

```
com.apple.developer.driverkit
com.apple.developer.driverkit.transport.pci
com.apple.developer.driverkit.family.networking  (if we register network ifaces)
```

## PCI device matching

The dext matches against Mellanox network controllers:

```xml
<!-- IOPersonalities in Info.plist -->
<key>IOPCIMatch</key>
<string>0x000015b3&0x0000ffff</string>  <!-- Any Mellanox device -->
<key>IOPCIClassMatch</key>
<string>0x02000000&0xffffff00</string>   <!-- Network controller -->
<key>IOPCITunnelCompatible</key>
<true/>                                   <!-- Works through Thunderbolt -->
```

**Coexistence concern:** Apple's `DriverKit-AppleEthernetMLX5` has the same
match pattern. Only one dext can claim a PCI device. We need to either:
1. Match on a more specific device ID (narrower than Apple's pattern)
2. Boot with Apple's dext disabled
3. Convince Apple to add a property that lets us coexist

This is a key research item (Phase 0).

## Userspace interface: `/dev/verbifrost/uverbs0`

The dext exposes a character device for userspace verbs access:

```
/dev/verbifrost/uverbs0   — First RDMA device (ConnectX port 0)
/dev/verbifrost/uverbs1   — Second RDMA device (ConnectX port 1)
```

Applications open this device and issue ioctls for RDMA operations.

### ioctl ABI (proposed)

```c
// Object lifecycle
VF_IOCTL_GET_CONTEXT       → query device, get fd
VF_IOCTL_ALLOC_PD          → Protection Domain
VF_IOCTL_DEALLOC_PD
VF_IOCTL_REG_MR            → Memory Region (DMA-map a buffer)
VF_IOCTL_DEREG_MR
VF_IOCTL_CREATE_CQ         → Completion Queue
VF_IOCTL_DESTROY_CQ
VF_IOCTL_CREATE_QP         → Queue Pair
VF_IOCTL_MODIFY_QP         → QP state machine (RST→INIT→RTR→RTS)
VF_IOCTL_DESTROY_QP
VF_IOCTL_CREATE_AH         → Address Handle (RoCEv2 GID/port)
VF_IOCTL_DESTROY_AH

// Data path
VF_IOCTL_POST_SEND         → Submit send work requests
VF_IOCTL_POST_RECV         → Submit receive work requests
VF_IOCTL_POLL_CQ           → Poll completion queue

// RoCEv2
VF_IOCTL_QUERY_GID         → Query GID table entry
VF_IOCTL_QUERY_PORT        → Query port state/speed
```

This mirrors the Linux uverbs ABI but with Darwin-native types (no
netlink, no udev, no `/sys` class).

## DMA mapping

Userspace buffers must be DMA-mapped for the NIC to access them:

```c
// Userspace: register a buffer for RDMA
struct vf_reg_mr req = {
    .addr = buf,
    .length = size,
    .access = VF_ACCESS_LOCAL_WRITE | VF_ACCESS_REMOTE_WRITE,
};
ioctl(fd, VF_IOCTL_REG_MR, &req);
// → dext calls IODMACommand to map buf for the NIC's DMA engine
// → returns mr_handle + lkey + rkey
```

On Apple Silicon, DMA goes through the DART (Apple's IOMMU). The dext must
set up the correct DART mappings so the ConnectX card can read/write the
physical memory behind the userspace virtual address.

## Event delivery

Completion events (CQ arm, async errors) need to reach userspace. Options:

1. **`IOUserClient` async callbacks** — the standard DriverKit mechanism
2. **Mach ports** — send completion notifications via Mach messages
3. **Shared memory + semaphore** — poll a shared-memory ring, signal via
   semaphore on completion

Option 3 is closest to Linux's `ibv_get_cq_event()` and likely fastest.
