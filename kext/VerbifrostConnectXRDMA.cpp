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
    volatile uint8_t *fInitSeg;

    struct mlx5_cmd_context fCmdCtx;

    bool initHardware(void);
    void freeHardware(void);

    IOReturn mapBAR0(void);
    IOReturn readInitSegment(void);
    IOReturn waitForInitializing(void);
    IOReturn initCmdQueue(void);
    IOReturn sendEnableHCA(void);
    IOReturn queryHcaCap(void);
    IOReturn queryPages(uint16_t type, int16_t *pages_needed);
    IOReturn initHca(void);

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

    ret = readInitSegment();
    if (ret != kIOReturnSuccess) { VF_ERROR("readInitSegment failed"); return false; }

    ret = waitForInitializing();
    if (ret != kIOReturnSuccess) { VF_ERROR("FW boot timeout"); return false; }
    VF_LOG("Firmware boot complete");

    /* Initialize the command queue (DMA ring + mailboxes) */
    ret = initCmdQueue();
    if (ret != kIOReturnSuccess) { VF_ERROR("cmd queue init failed"); return false; }
    VF_LOG("Command queue initialized");

    /* Send ENABLE_HCA — the first real firmware command */
    ret = sendEnableHCA();
    if (ret != kIOReturnSuccess) { VF_ERROR("ENABLE_HCA failed"); return false; }
    VF_LOG("ENABLE_HCA succeeded — HCA is active!");

    /* Query boot pages that firmware needs */
    int16_t boot_pages = 0;
    ret = queryPages(1, &boot_pages);  /* type=1 = boot pages */
    if (ret != kIOReturnSuccess) {
        VF_ERROR("QUERY_PAGES failed");
        return false;
    }
    VF_LOG("Firmware needs %d boot pages", boot_pages);

    /* Query device capabilities */
    ret = queryHcaCap();
    if (ret != kIOReturnSuccess) {
        VF_ERROR("QUERY_HCA_CAP failed");
        return false;
    }

    /* Initialize the HCA */
    ret = initHca();
    if (ret != kIOReturnSuccess) {
        VF_ERROR("INIT_HCA failed");
        return false;
    }
    VF_LOG("HCA fully initialized!");

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
    fInitSeg = fBAR0 + MLX5_INIT_SEG_OFFSET;
    return kIOReturnSuccess;
}

/* ============================================================
 * readInitSegment — Read and log firmware version + cmdq params
 * ============================================================ */

IOReturn VerbifrostConnectXRDMA::readInitSegment(void)
{
    if (!fInitSeg) return kIOReturnNotReady;

    volatile uint32_t *base = (volatile uint32_t *)fInitSeg;
    uint32_t fw_major    = OSSwapInt32(base[0]);
    uint32_t fw_minor    = OSSwapInt32(base[1]);
    uint32_t fw_subminor = OSSwapInt32(base[2]);
    uint32_t cmdif_rev   = OSSwapInt32(base[3]);

    uint32_t cmdq_params = OSSwapInt32(base[4]);
    uint32_t log_size = (cmdq_params >> MLX5_CMDQ_LOG_SIZE_SHIFT) & MLX5_CMDQ_LOG_SIZE_MASK;
    uint32_t log_stride = (cmdq_params >> MLX5_CMDQ_LOG_STRIDE_SHIFT) & MLX5_CMDQ_LOG_STRIDE_MASK;

    VF_LOG("HCA FW version: %d.%d.%04d cmdif_rev=%d",
           fw_major, fw_minor, fw_subminor, cmdif_rev);
    VF_LOG("Command queue: log_size=%d log_stride=%d (%u entries x %u bytes)",
           log_size, log_stride, 1 << log_size, 1 << log_stride);

    return kIOReturnSuccess;
}

/* ============================================================
 * waitForInitializing — Wait for 'initializing' bit to clear
 * ============================================================ */

IOReturn VerbifrostConnectXRDMA::waitForInitializing(void)
{
    volatile uint32_t *init_ptr =
        (volatile uint32_t *)(fInitSeg + MLX5_ISEG_INITIALIZING);
    for (int i = 0; i < 1000; i++) {
        uint32_t val = OSSwapInt32(*init_ptr);
        if (!(val & MLX5_ISEG_INITIALIZING_BIT)) return kIOReturnSuccess;
        IOSleep(10);
    }
    return kIOReturnTimeout;
}

/* ============================================================
 * initCmdQueue — Initialize the mlx5 command queue
 * ============================================================ */

IOReturn VerbifrostConnectXRDMA::initCmdQueue(void)
{
    return mlx5_cmd_init(&fCmdCtx, fBAR0, fBAR0Size);
}

/* ============================================================
 * sendEnableHCA — Send ENABLE_HCA (opcode 0x104) to the firmware
 * ============================================================ */

IOReturn VerbifrostConnectXRDMA::sendEnableHCA(void)
{
    /* ENABLE_HCA input:
     *   offset 0: opcode = 0x104
     *   offset 2: op_mod = 0 (0=driver, 1=mlx5driver)
     *   offset 4: embedded_cpu_function (0 for NIC PF)
     *   offset 8: function_id (0 for PF)
     *
     * ENABLE_HCA output:
     *   offset 0: status
     *   offset 8: assigned_function_id
     *   offset 0x0C: reserved
     */
    uint8_t in_buf[16]  = {0};
    uint8_t out_buf[64] = {0};

    uint8_t fw_status = 0xFF;
    IOReturn ret = mlx5_cmd_exec(&fCmdCtx,
                                 MLX5_CMD_OP_ENABLE_HCA, 0,
                                 in_buf, 0,
                                 out_buf, sizeof(out_buf),
                                 &fw_status);

    if (ret == kIOReturnSuccess) {
        /* Extract assigned function ID from response */
        uint16_t func_id = OSSwapInt16(*(uint16_t *)(out_buf + 8));
        VF_LOG("ENABLE_HCA: function_id=%d", func_id);
        setProperty("HCAFunctionID", func_id, 16);
    }

    return ret;
}

/* ============================================================
 * queryPages — QUERY_PAGES command (opcode 0x107)
 * Asks the firmware how many pages it needs (boot/init).
 * ============================================================ */

IOReturn VerbifrostConnectXRDMA::queryPages(uint16_t type, int16_t *pages_needed)
{
    /* QUERY_PAGES input:
     *   offset 0: opcode = 0x107
     *   offset 6: op_mod = type (1=boot, 2=init, 3=regular)
     *
     * QUERY_PAGES output:
     *   offset 0: status
     *   offset 8: num_pages (signed 16-bit, big-endian)
     */
    uint8_t out_buf[16] = {0};
    uint8_t fw_status = 0xFF;

    IOReturn ret = mlx5_cmd_exec(&fCmdCtx,
                                 MLX5_CMD_OP_QUERY_PAGES, type,
                                 NULL, 0,
                                 out_buf, sizeof(out_buf),
                                 &fw_status);

    if (ret == kIOReturnSuccess) {
        *pages_needed = (int16_t)OSSwapInt16(*(uint16_t *)(out_buf + 8));
    }

    return ret;
}

/* ============================================================
 * queryHcaCap — QUERY_HCA_CAP (opcode 0x100)
 * Reads device capabilities (max QPs, max CQs, port types, etc.)
 * ============================================================ */

IOReturn VerbifrostConnectXRDMA::queryHcaCap(void)
{
    /* QUERY_HCA_CAP input:
     *   offset 0: opcode = 0x100
     *   offset 6: op_mod = capability type
     *     bits [1:0] = cap group (0=general, 1=eth offload, 2=odp, 3=atomic)
     *     bit 2 = 0 (query current), 1 (query max)
     *
     * QUERY_HCA_CAP output:
     *   offset 0:  status (8-bit)
     *   offset 8:  capability struct (cmd_hca_cap, ~1000 bytes)
     */

    /* Query general capabilities (cap group 0) */
    uint8_t out_buf[1024] = {0};
    uint8_t fw_status = 0xFF;

    IOReturn ret = mlx5_cmd_exec(&fCmdCtx,
                                 MLX5_CMD_OP_QUERY_HCA_CAP, 0x00,
                                 NULL, 0,
                                 out_buf, sizeof(out_buf),
                                 &fw_status);

    if (ret != kIOReturnSuccess) {
        VF_LOG("QUERY_HCA_CAP failed: fw_status=0x%02x", fw_status);
        return ret;
    }

    /* Parse key capabilities from the response.
     * The capability struct starts at offset 8 in the output mailbox.
     * Key fields (from mlx5_ifc cmd_hca_cap, all big-endian bitfields):
     *   offset 8+0x00: log_max_qp (bits [5:0] of byte at +0x18)
     *   offset 8+0x01: num_ports (bits [2:0] of byte at +0x61)
     *   offset 8+0x02: log_max_cq
     *   offset 8+0x03: log_max_mkey
     */

    /* The capability is a bitfield struct. Key offsets:
     * num_ports: offset 0x61 bits [2:0]
     * log_max_qp: offset 0x18 bits [5:0]
     * log_max_cq: offset 0x98 bits [4:0]
     */
    uint8_t *cap = out_buf + 8;  /* skip mbox header */

    /* num_ports (offset 0x61, low 3 bits) */
    uint8_t num_ports = cap[0x61] & 0x7;

    /* log_max_qp (offset 0x18, low 6 bits) */
    uint8_t log_max_qp = cap[0x18] & 0x3F;

    VF_LOG("HCA Capabilities: ports=%d, max_qp=%d (%d QPs)",
           num_ports, log_max_qp, 1 << log_max_qp);

    /* Store as IORegistry properties for userspace visibility */
    setProperty("HCANumPorts", num_ports, 8);
    setProperty("HCALogMaxQP", log_max_qp, 8);
    setProperty("HCAMaxQPs", 1 << log_max_qp, 32);

    return kIOReturnSuccess;
}

/* ============================================================
 * initHca — INIT_HCA (opcode 0x103)
 * Initializes HCA internal resources. For Phase 1 we send a
 * minimal INIT_HCA (defaults).
 * ============================================================ */

IOReturn VerbifrostConnectXRDMA::initHca(void)
{
    /* INIT_HCA input:
     *   offset 0: opcode = 0x103
     *   offset 6: op_mod = 0
     *   offset 8+: init_hca_in struct (can be mostly zeros for defaults)
     *
     * INIT_HCA output:
     *   offset 0: status
     */
    uint8_t in_buf[512]  = {0};
    uint8_t out_buf[16]  = {0};
    uint8_t fw_status = 0xFF;

    IOReturn ret = mlx5_cmd_exec(&fCmdCtx,
                                 MLX5_CMD_OP_INIT_HCA, 0,
                                 in_buf + 16, sizeof(in_buf) - 16,
                                 out_buf, sizeof(out_buf),
                                 &fw_status);

    if (ret == kIOReturnSuccess) {
        VF_LOG("INIT_HCA succeeded");
    }

    return ret;
}

void VerbifrostConnectXRDMA::freeHardware(void)
{
    mlx5_cmd_cleanup(&fCmdCtx);
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

