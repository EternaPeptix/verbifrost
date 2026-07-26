/*
 * mlx5_mr.cpp — Memory Registration implementation.
 *
 * Maps a userspace buffer for RDMA access by:
 *   1. Creating a DMA page list (Physical Address Array - pas)
 *   2. Sending CREATE_MKEY command with the pas
 *   3. Firmware creates MTT entries and returns lkey/rkey
 *
 * Once registered, the memory can be referenced by remote nodes
 * via RDMA READ/WRITE using the rkey, or locally via the lkey.
 */

#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/OSByteOrder.h>

#include "mlx5_registers.h"
#include "mlx5_cmd.h"
#include "mlx5_mr.h"

#define MLX5_MR_LOG(fmt, ...) do { IOLog("mlx5_mr: " fmt "\n", ##__VA_ARGS__); } while(0)

/* ============================================================
 * build_pas_list — Build a Physical Address Array
 *
 * For kernel-allocated memory, we use the buffer's physical pages.
 * For userspace memory (the real use case), we'd call
 * IOMemoryDescriptor::prepare() + iterate physical segments.
 * ============================================================ */
static uint64_t *build_pas_list(uint64_t base_addr, uint32_t length,
                                uint32_t *out_npages,
                                IOBufferMemoryDescriptor **out_md)
{
    /* Page size is 4KB (ConnectX supports up to 2MB hugepages) */
    uint32_t page_size = 4096;
    uint32_t npages = (length + page_size - 1) / page_size;
    if (npages == 0) npages = 1;

    /* Allocate pas array (8 bytes per entry, big-endian) */
    uint32_t pas_size = npages * 8;
    /* Round up to 4KB for DMA alignment */
    uint32_t pas_alloc = (pas_size + 4095) & ~4095;

    IOBufferMemoryDescriptor *pas_md =
        IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
            kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut,
            pas_alloc, 0xFFFFFFFFFFFFF000ULL);
    if (!pas_md) {
        MLX5_MR_LOG("pas alloc failed (%u bytes)", pas_alloc);
        return NULL;
    }

    uint64_t *pas = (uint64_t *)pas_md->getBytesNoCopy();
    memset(pas, 0, pas_alloc);

    /* Fill physical addresses.
     * For contiguous kernel memory, each page is base_addr + i*4096.
     * In the real implementation with userspace memory, we'd use:
     *   IOMemoryDescriptor *umd = IOMemoryDescriptor::withAddressRange(...)
     *   umd->prepare()
     *   for each segment: add physical addresses to pas
     */
    for (uint32_t i = 0; i < npages; i++) {
        pas[i] = OSSwapInt64(base_addr + (uint64_t)i * page_size);
    }

    *out_npages = npages;
    *out_md = pas_md;
    return pas;
}

/* ============================================================
 * mlx5_ib_reg_user_mr_full — Register userspace memory for RDMA
 * ============================================================ */
struct ib_mr *mlx5_ib_reg_user_mr_full(struct ib_pd *pd, uint64_t start,
                                       uint64_t length, uint64_t virt_addr,
                                       int access_flags,
                                       struct mlx5_cmd_context *cmd_ctx)
{
    MLX5_MR_LOG("reg_user_mr: start=0x%llx len=%llu iova=0x%llx flags=0x%x",
                start, length, virt_addr, access_flags);

    /* Step 1: Build physical address list */
    uint32_t npages = 0;
    IOBufferMemoryDescriptor *pas_md = NULL;
    uint64_t *pas = build_pas_list(start, (uint32_t)length, &npages, &pas_md);
    if (!pas) return NULL;

    uint64_t pas_dma = pas_md->getPhysicalAddress();
    MLX5_MR_LOG("pas: %u pages @ DMA 0x%llx", npages, pas_dma);

    /* Step 2: Build CREATE_MKEY command input */
    uint8_t in_buf[512] = {0};
    uint8_t out_buf[64]  = {0};

    /* The MKC (Memory Key Context) starts after the CREATE_MKEY header.
     * Layout: [create_mkey_in_header(16)] [mkc(~100 bytes)] [pas[]]
     */
    uint8_t *mkc = in_buf + 16;  /* MKC offset */

    /* access_mode = MTT (1) */
    mkc[MLX5_MKC_OFF_ACCESS_MODE] = MLX5_MKC_ACCESS_MODE_MTT;

    /* access flags: map ib_access_flags to mlx5 bits
     *   IB_ACCESS_LOCAL_WRITE  → a.access_mode_local_write
     *   IB_ACCESS_REMOTE_WRITE → a.remote_write
     *   IB_ACCESS_REMOTE_READ  → a.remote_read
     */
    uint32_t mlx5_access = 0;
    if (access_flags & IB_ACCESS_LOCAL_WRITE)  mlx5_access |= 1;
    if (access_flags & IB_ACCESS_REMOTE_WRITE) mlx5_access |= 2;
    if (access_flags & IB_ACCESS_REMOTE_READ)  mlx5_access |= 4;
    *(uint32_t *)(mkc + MLX5_MKC_OFF_A_FLAGS) = OSSwapInt32(mlx5_access);

    /* length (64-bit) */
    *(uint64_t *)(mkc + MLX5_MKC_OFF_LEN) = OSSwapInt64(length);

    /* I/O virtual address (64-bit) */
    *(uint64_t *)(mkc + MLX5_MKC_OFF_IOVA) = OSSwapInt64(virt_addr);

    /* PD number */
    if (pd) {
        uint32_t pdn = pd->local_dma_lkey;
        *(uint32_t *)(mkc + MLX5_MKC_OFF_PD) = OSSwapInt32(pdn);
    }

    /* log_page_size = 12 (4KB pages) */
    mkc[MLX5_MKC_OFF_LOG_PAGE_SIZE] = 12;

    /* Physical address array follows the MKC */
    *(uint64_t *)(in_buf + 0x100) = OSSwapInt64(pas_dma);

    /* Step 3: Send CREATE_MKEY command */
    uint8_t fw_status = 0xFF;
    IOReturn ret = mlx5_cmd_exec(cmd_ctx,
                                 MLX5_CMD_OP_CREATE_MKEY, 0,
                                 in_buf, 0x110,
                                 out_buf, sizeof(out_buf),
                                 &fw_status);
    if (ret != kIOReturnSuccess) {
        MLX5_MR_LOG("CREATE_MKEY failed: fw_status=0x%02x", fw_status);
        pas_md->release();
        return NULL;
    }

    /* Step 4: Extract mkey from response */
    uint32_t mkey_idx = OSSwapInt32(*(uint32_t *)(out_buf + 8));

    /* Allocate mlx5_ib_mr */
    struct mlx5_ib_mr *mr = (struct mlx5_ib_mr *)IOMalloc(sizeof(struct mlx5_ib_mr));
    if (!mr) {
        pas_md->release();
        return NULL;
    }
    memset(mr, 0, sizeof(*mr));

    mr->mkey = mkey_idx;
    mr->mtt_level = 0;
    mr->pas = pas;
    mr->npages = npages;
    mr->pas_md = pas_md;

    /* Set ib_mr fields */
    mr->ibmr.device = pd->device;
    mr->ibmr.pd = pd;
    mr->ibmr.lkey = mkey_idx << 8;     /* lkey = mkey_index << 8 */
    mr->ibmr.rkey = mr->ibmr.lkey;      /* For MR, lkey == rkey */
    mr->ibmr.iova = virt_addr;
    mr->ibmr.length = length;

    MLX5_MR_LOG("MR created: mkey=0x%x lkey=0x%x rkey=0x%x (%u pages)",
                mkey_idx, mr->ibmr.lkey, mr->ibmr.rkey, npages);

    return &mr->ibmr;
}
