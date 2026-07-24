/*
 * VerbifrostConnectXRDMA.cpp — IOKit kext that matches the ConnectX-6 LX
 * PCI device and initializes the mlx5 HCA for RDMA.
 *
 * Phase 1 of Verbifrost: getting the ConnectX card initialized
 * so it can be registered with IORDMAFamily as an RDMA provider.
 *
 * Initialization sequence (mlx5 firmware spec):
 *   1. Map BAR0 (init segment + command queues)
 *   2. Read HCA capabilities from the init segment
 *   3. Wait for firmware to finish booting
 *   4. Request boot pages
 *   5. Allocate command interface (DMA ring)
 *   6. Send ENABLE_HCA command
 *   7. Query/initialize HCA (INIT_HCA)
 *   8. (Phase 2) Register with IORDMAFamily via ib_register_device
 *
 * Build: make
 * Load:  kextload VerbifrostConnectX.kext (requires SIP disabled)
 */

#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOLib.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/OSByteOrder.h>

#include "mlx5_registers.h"
#include "mlx5_cmd.h"

#define VF_LOG(fmt, ...) \
    IOLog("VerbifrostConnectX: " fmt "\n", ##__VA_ARGS__)
#define VF_ERROR(fmt, ...) \
    IOLog("VerbifrostConnectX ERROR: " fmt "\n", ##__VA_ARGS__)
#define VF_DEBUG(fmt, ...) do { \
    IOLog("VerbifrostConnectX [dbg]: " fmt "\n", ##__VA_ARGS__); \
} while(0)

/* ============================================================
 * VerbifrostConnectXRDMA — IOKit service for the ConnectX card
 * ============================================================ */

class VerbifrostConnectXRDMA : public IOService
{
    OSDeclareDefaultStructors(VerbifrostConnectXRDMA);

private:
    IOPCIDevice      *fPCIDevice;
    IOMemoryMap      *fBAR0Map;
    volatile uint8_t *fBAR0;
    uint32_t          fBAR0Size;

    volatile struct mlx5_init_seg *fInitSeg;

    bool initHardware(void);
    void freeHardware(void);

    IOReturn mapBAR0(void);
    IOReturn readInitSegment(struct mlx5_init_seg *seg);
    IOReturn waitForInitializing(void);
    IOReturn requestBootPages(void);
    IOReturn verifyCmdInterface(void);

public:
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    virtual void free(void) override;
};

OSDefineMetaClassAndStructors(VerbifrostConnectXRDMA, IOService);

/* ============================================================
 * start() — Called when the PCI device matches our personality
 * ============================================================ */

bool VerbifrostConnectXRDMA::start(IOService *provider)
{
    VF_LOG("start() — ConnectX RDMA provider initializing");

    if (!IOService::start(provider)) {
        VF_ERROR("super::start() failed");
        return false;
    }

    fPCIDevice = OSDynamicCast(IOPCIDevice, provider);
    if (!fPCIDevice) {
        VF_ERROR("provider is not IOPCIDevice");
        return false;
    }
    fPCIDevice->retain();

    uint16_t vendorID = fPCIDevice->configRead16(kIOPCIConfigVendorID);
    uint16_t deviceID = fPCIDevice->configRead16(kIOPCIConfigDeviceID);
    uint16_t subVendor = fPCIDevice->configRead16(kIOPCIConfigSubSystemVendorID);
    uint16_t subDevice = fPCIDevice->configRead16(kIOPCIConfigSubSystemID);
    uint8_t  revision = fPCIDevice->configRead8(kIOPCIConfigRevisionID);

    const char *chipName = "unknown";
    if (deviceID == MLX5_DEV_CX6_LX) chipName = "ConnectX-6 LX";
    else if (deviceID == MLX5_DEV_CX6) chipName = "ConnectX-6";
    else if (deviceID == MLX5_DEV_CX7) chipName = "ConnectX-7";

    VF_LOG("PCI Device: %s (vendor=0x%04x device=0x%04x "
           "sub=0x%04x/0x%04x rev=0x%02x)",
           chipName, vendorID, deviceID, subVendor, subDevice, revision);

    // Enable the PCI device (memory space + bus master)
    fPCIDevice->configWrite16(kIOPCIConfigCommand,
        fPCIDevice->configRead16(kIOPCIConfigCommand) |
        kIOPCICommandMemorySpace | kIOPCICommandBusMaster);

    if (!initHardware()) {
        VF_ERROR("initHardware() failed");
        stop(provider);
        return false;
    }

    setProperty("VerbifrostRDMA", kOSBooleanTrue);
    setProperty("RDMAProvider", "VerbifrostConnectX");
    setProperty("VendorID", vendorID, 16);
    setProperty("DeviceID", deviceID, 16);
    registerService();

    VF_LOG("start() — ConnectX RDMA provider ready");
    return true;
}

/* ============================================================
 * initHardware — Full mlx5 bringup sequence
 * ============================================================ */

bool VerbifrostConnectXRDMA::initHardware(void)
{
    IOReturn ret;

    ret = mapBAR0();
    if (ret != kIOReturnSuccess) { VF_ERROR("mapBAR0 failed"); return false; }

    struct mlx5_init_seg seg;
    memset(&seg, 0, sizeof(seg));
    ret = readInitSegment(&seg);
    if (ret != kIOReturnSuccess) { VF_ERROR("readInitSegment failed"); return false; }

    VF_LOG("HCA FW version: %d.%d.%04d",
           seg.fw_rev_major, seg.fw_rev_minor, seg.fw_rev_subminor);
    VF_LOG("Command queue: strider=%d max_lgth=%d",
           seg.log_cmd_strider, seg.log_cmd_max_lgth_sz);

    ret = waitForInitializing();
    if (ret != kIOReturnSuccess) { VF_ERROR("FW boot timeout"); return false; }
    VF_LOG("Firmware boot complete");

    ret = requestBootPages();
    if (ret != kIOReturnSuccess) { VF_ERROR("boot pages failed"); return false; }
    VF_LOG("Boot pages allocated");

    ret = verifyCmdInterface();
    if (ret != kIOReturnSuccess) { VF_ERROR("cmd interface failed"); return false; }
    VF_LOG("Command interface verified");

    VF_LOG("Hardware initialized — ENABLE_HCA pending DMA ring setup");
    return true;
}


/* ============================================================
 * mapBAR0 — Map BAR0 (32MB on ConnectX-6 LX)
 * ============================================================ */

IOReturn VerbifrostConnectXRDMA::mapBAR0(void)
{
    IOMemoryDescriptor *barMD = fPCIDevice->getDeviceMemoryWithIndex(0);
    if (!barMD) {
        VF_ERROR("BAR0 not found");
        return kIOReturnNoResources;
    }

    fBAR0Size = (uint32_t)barMD->getLength();
    VF_LOG("BAR0: phys=0x%llx size=%u (%uMB)",
           barMD->getPhysicalAddress(), fBAR0Size, fBAR0Size / (1024*1024));

    fBAR0Map = barMD->map();
    if (!fBAR0Map) {
        VF_ERROR("Failed to map BAR0");
        return kIOReturnNoResources;
    }

    fBAR0 = (volatile uint8_t *)fBAR0Map->getVirtualAddress();
    fInitSeg = (volatile struct mlx5_init_seg *)(fBAR0 + MLX5_INIT_SEG_OFFSET);
    return kIOReturnSuccess;
}

/* ============================================================
 * readInitSegment — Read firmware version and command queue params
 * ============================================================ */

IOReturn VerbifrostConnectXRDMA::readInitSegment(struct mlx5_init_seg *seg)
{
    if (!fInitSeg) return kIOReturnNotReady;

    volatile uint32_t *base = (volatile uint32_t *)fInitSeg;

    seg->fw_rev_major      = OSSwapInt32(base[0]);
    seg->fw_rev_minor      = OSSwapInt32(base[1]);
    seg->fw_rev_subminor   = OSSwapInt32(base[2]);
    seg->cmd_interface_rev = OSSwapInt32(base[3]);

    uint32_t cmdq = OSSwapInt32(base[5]);
    seg->log_cmd_strider     = (cmdq >> 8) & 0xFF;
    seg->log_cmd_max_lgth_sz = (cmdq >> 24) & 0x1F;

    uint32_t init_state = OSSwapInt32(base[0x7F]);
    seg->initializing = (init_state >> 31) & 1;
    return kIOReturnSuccess;
}

/* ============================================================
 * waitForInitializing — Wait for 'initializing' bit to clear
 * ============================================================ */

IOReturn VerbifrostConnectXRDMA::waitForInitializing(void)
{
    volatile uint32_t *init_ptr =
        (volatile uint32_t *)((uint8_t *)fInitSeg + 0x1FC);
    for (int i = 0; i < 1000; i++) {
        uint32_t val = OSSwapInt32(*init_ptr);
        if (!((val >> 31) & 1)) return kIOReturnSuccess;
        IOSleep(10);
    }
    return kIOReturnTimeout;
}


/* ============================================================
 * requestBootPages — Allocate 64KB DMA page for firmware boot
 * ============================================================ */

IOReturn VerbifrostConnectXRDMA::requestBootPages(void)
{
    IOBufferMemoryDescriptor *bootMD =
        IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
            kernel_task,
            kIOMemoryPhysicallyContiguous | kIODirectionInOut,
            MLX5_BOOT_PAGE_SIZE,
            0xFFFFFFFFFFFFF000ULL);
    if (!bootMD) { VF_ERROR("boot page alloc failed"); return kIOReturnNoMemory; }
    memset(bootMD->getBytesNoCopy(), 0, MLX5_BOOT_PAGE_SIZE);

    IODMACommand *dmaCmd = IODMACommand::withSpecification(
        kIODMACommandOutputHost64, 64, 0,
        IODMACommand::kMapped, MLX5_BOOT_PAGE_SIZE, 1);
    if (!dmaCmd) { bootMD->release(); return kIOReturnNoResources; }

    IOReturn ret = dmaCmd->setMemoryDescriptor(bootMD);
    if (ret != kIOReturnSuccess) {
        dmaCmd->release(); bootMD->release(); return ret;
    }

    IODMACommand::Segment64 seg;
    seg.fIOVMAddr = 0;
    seg.fLength = 0;
    uint64_t segLen = 1;  // 1 segment
    ret = dmaCmd->gen64IOVMSegments(&segLen, &seg, NULL);
    if (ret != kIOReturnSuccess || segLen == 0) {
        dmaCmd->clearMemoryDescriptor(); dmaCmd->release(); bootMD->release();
        return ret ? ret : kIOReturnError;
    }
    uint64_t segAddr = seg.fIOVMAddr;

    VF_DEBUG("Boot page DMA: 0x%llx", segAddr);

    volatile uint64_t *boot_addr =
        (volatile uint64_t *)((uint8_t *)fInitSeg + MLX5_INIT_SEG_BOOT_ADDR_OFFSET);
    *boot_addr = OSSwapInt64(segAddr);

    volatile uint32_t *nic_if =
        (volatile uint32_t *)((uint8_t *)fInitSeg + MLX5_INIT_SEG_NIC_IF_OFFSET);
    uint32_t val = OSSwapInt32(*nic_if);
    *nic_if = OSSwapInt32(val | (1U << 6));

    IOSleep(100);
    setProperty("BootPagePhys", segAddr, 64);

    dmaCmd->clearMemoryDescriptor();
    dmaCmd->release();
    bootMD->release();
    return kIOReturnSuccess;
}

/* ============================================================
 * verifyCmdInterface — Verify the command queue is accessible
 * ============================================================ */

IOReturn VerbifrostConnectXRDMA::verifyCmdInterface(void)
{
    struct mlx5_init_seg seg;
    memset(&seg, 0, sizeof(seg));
    IOReturn ret = readInitSegment(&seg);
    if (ret != kIOReturnSuccess) return ret;

    uint32_t entries = 1 << seg.log_cmd_strider;
    VF_LOG("CmdQ: %u entries", entries);

    volatile uint32_t *cmdq = (volatile uint32_t *)(fBAR0 + MLX5_CMDQ_OFFSET);
    uint32_t val = OSSwapInt32(cmdq[0]);
    VF_DEBUG("CmdQ[0] = 0x%08x", val);
    return kIOReturnSuccess;
}

/* ============================================================
 * Cleanup
 * ============================================================ */

void VerbifrostConnectXRDMA::freeHardware(void)
{
    if (fBAR0Map) { fBAR0Map->release(); fBAR0Map = nullptr; }
    if (fPCIDevice) { fPCIDevice->release(); fPCIDevice = nullptr; }
}

void VerbifrostConnectXRDMA::stop(IOService *provider)
{
    VF_LOG("stop()");
    freeHardware();
    IOService::stop(provider);
}

void VerbifrostConnectXRDMA::free(void)
{
    IOService::free();
}

