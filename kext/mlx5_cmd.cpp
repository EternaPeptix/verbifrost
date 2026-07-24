/*
 * mlx5_cmd.cpp — Command queue implementation for mlx5 HCA.
 *
 * This implements the command interface that lets the driver talk
 * to the firmware. The protocol:
 *
 * 1. Driver allocates a DMA ring of command entries (each 64 bytes)
 * 2. Driver writes the ring's DMA address to the init segment
 * 3. To send a command:
 *    a. Find a free ring slot
 *    b. Allocate input + output mailboxes (4KB DMA buffers)
 *    c. Fill input mailbox with command header + data
 *    d. Write mailbox DMA addresses to the ring entry
 *    e. Set owner bit to HW, ring doorbell
 *    f. Poll the entry's status until owner flips back to SW
 *    g. Read response from output mailbox
 *
 * Ported from Linux: drivers/net/ethernet/mellanox/mlx5/core/cmd.c
 */

#include <IOKit/IOLib.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/OSByteOrder.h>

#include "mlx5_registers.h"
#include "mlx5_cmd.h"

#define MLX5_LOG(fmt, ...) \
    IOLog("mlx5_cmd: " fmt "\
", ##__VA_ARGS__)

/* ============================================================
 * Helper: read big-endian 32 from init segment
 * ============================================================ */
static inline uint32_t iseg_read32(volatile uint8_t *iseg, uint32_t offset)
{
    volatile uint32_t *p = (volatile uint32_t *)(iseg + offset);
    return OSSwapInt32(*p);
}

static inline void iseg_write32(volatile uint8_t *iseg, uint32_t offset, uint32_t val)
{
    volatile uint32_t *p = (volatile uint32_t *)(iseg + offset);
    *p = OSSwapInt32(val);
}

static inline void iseg_write64(volatile uint8_t *iseg, uint32_t offset, uint64_t val)
{
    iseg_write32(iseg, offset,     (uint32_t)(val & 0xFFFFFFFF));
    iseg_write32(iseg, offset + 4, (uint32_t)(val >> 32));
}

/* ============================================================
 * mlx5_cmd_init — Allocate command ring + mailboxes, register with FW
 * ============================================================ */
IOReturn mlx5_cmd_init(struct mlx5_cmd_context *ctx,
                       volatile uint8_t *bar0, uint32_t bar0_size)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->bar0 = bar0;
    ctx->bar0_size = bar0_size;
    ctx->iseg = bar0 + MLX5_INIT_SEG_OFFSET;

    /* Read command queue parameters from init segment */
    uint32_t cmdq_params = iseg_read32(ctx->iseg, MLX5_ISEG_CMDQ_PARAMS);
    uint32_t log_size    = (cmdq_params >> MLX5_CMDQ_LOG_SIZE_SHIFT) & MLX5_CMDQ_LOG_SIZE_MASK;
    uint32_t log_stride  = (cmdq_params >> MLX5_CMDQ_LOG_STRIDE_SHIFT) & MLX5_CMDQ_LOG_STRIDE_MASK;

    ctx->num_entries   = 1 << log_size;
    ctx->entry_stride  = 1 << log_stride;

    MLX5_LOG("CmdQ params: log_size=%d log_stride=%d => %u entries x %u bytes",
             log_size, log_stride, ctx->num_entries, ctx->entry_stride);

    if (ctx->num_entries > MLX5_MAX_CMD_ENTRIES) {
        MLX5_LOG("WARNING: firmware reports %u entries, clamping to %d",
                 ctx->num_entries, MLX5_MAX_CMD_ENTRIES);
        ctx->num_entries = MLX5_MAX_CMD_ENTRIES;
    }

    /* Allocate the command ring DMA memory */
    uint32_t ring_size = ctx->num_entries * ctx->entry_stride;
    ctx->cmd_ring_md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task,
        kIOMemoryPhysicallyContiguous | kIODirectionInOut,
        ring_size,
        0xFFFFFFFFFFFFF000ULL);

    if (!ctx->cmd_ring_md) {
        MLX5_LOG("ERROR: failed to allocate command ring (%u bytes)", ring_size);
        return kIOReturnNoMemory;
    }

    memset(ctx->cmd_ring_md->getBytesNoCopy(), 0, ring_size);
    ctx->cmd_ring = (volatile struct mlx5_cmd_prot_block *)
        ctx->cmd_ring_md->getBytesNoCopy();

    /* Get DMA address via IODMACommand */
    IODMACommand *dmaCmd = IODMACommand::withSpecification(
        kIODMACommandOutputHost64, 64, 0,
        IODMACommand::kMapped, ring_size, 1);
    if (!dmaCmd) {
        ctx->cmd_ring_md->release();
        return kIOReturnNoResources;
    }

    IOReturn ret = dmaCmd->setMemoryDescriptor(ctx->cmd_ring_md);
    if (ret != kIOReturnSuccess) {
        dmaCmd->release();
        ctx->cmd_ring_md->release();
        return ret;
    }

    IODMACommand::Segment64 seg;
    seg.fIOVMAddr = 0; seg.fLength = 0;
    uint64_t segCount = 1;
    ret = dmaCmd->gen64IOVMSegments(&segCount, &seg, NULL);
    dmaCmd->clearMemoryDescriptor();
    dmaCmd->release();

    if (ret != kIOReturnSuccess || segCount == 0) {
        ctx->cmd_ring_md->release();
        return ret ? ret : kIOReturnError;
    }
    ctx->cmd_ring_dma = seg.fIOVMAddr;

    MLX5_LOG("Command ring DMA: 0x%llx (%u bytes)", ctx->cmd_ring_dma, ring_size);

    /* Initialize all ring entries: owner = SW, token = 0xFF (invalid) */
    for (uint32_t i = 0; i < ctx->num_entries; i++) {
        volatile struct mlx5_cmd_prot_block *entry =
            (volatile struct mlx5_cmd_prot_block *)
            ((uint8_t *)ctx->cmd_ring + i * ctx->entry_stride);
        entry->status = MLX5_CMD_OWNER_SW;
        entry->token = 0xFFFFFFFF;
    }

    /* Register the command ring address with the firmware */
    iseg_write32(ctx->iseg, MLX5_ISEG_CMDQ_ADDR_L,
                 (uint32_t)(ctx->cmd_ring_dma & 0xFFFFFFFF));
    iseg_write32(ctx->iseg, MLX5_ISEG_CMDQ_ADDR_H,
                 (uint32_t)(ctx->cmd_ring_dma >> 32));

    MLX5_LOG("CmdQ DMA addr written to init segment");

    /* Allocate mailbox pool: 2 per entry (in + out) */
    ctx->num_mailboxes = ctx->num_entries * 2;
    uint32_t mbox_pool_size = ctx->num_mailboxes * MLX5_CMD_MAILBOX_SIZE;
    ctx->mbox_md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task,
        kIOMemoryPhysicallyContiguous | kIODirectionInOut,
        mbox_pool_size,
        0xFFFFFFFFFFFFF000ULL);

    if (!ctx->mbox_md) {
        MLX5_LOG("ERROR: failed to allocate mailbox pool (%u bytes)", mbox_pool_size);
        ctx->cmd_ring_md->release();
        return kIOReturnNoMemory;
    }

    memset(ctx->mbox_md->getBytesNoCopy(), 0, mbox_pool_size);
    ctx->mbox_pool = (uint8_t *)ctx->mbox_md->getBytesNoCopy();

    /* Get mailbox pool DMA address */
    IODMACommand *mboxDma = IODMACommand::withSpecification(
        kIODMACommandOutputHost64, 64, 0,
        IODMACommand::kMapped, mbox_pool_size, 1);
    if (mboxDma) {
        IOReturn r = mboxDma->setMemoryDescriptor(ctx->mbox_md);
        if (r == kIOReturnSuccess) {
            IODMACommand::Segment64 mseg;
            mseg.fIOVMAddr = 0; mseg.fLength = 0;
            uint64_t mcount = 1;
            r = mboxDma->gen64IOVMSegments(&mcount, &mseg, NULL);
            if (r == kIOReturnSuccess && mcount > 0)
                ctx->mbox_dma_base = mseg.fIOVMAddr;
        }
        mboxDma->clearMemoryDescriptor();
        mboxDma->release();
    }

    MLX5_LOG("Mailbox pool: %u buffers @ DMA 0x%llx",
             ctx->num_mailboxes, ctx->mbox_dma_base);

    /* Ring all doorbells to let firmware know the ring is ready */
    iseg_write32(ctx->iseg, MLX5_ISEG_CMD_DOORBELL, 0xFFFFFFFF);
    IOSleep(10);

    MLX5_LOG("Command queue initialized and registered with firmware");
    return kIOReturnSuccess;
}

/* ============================================================
 * mlx5_cmd_exec — Send a command and poll for completion
 * ============================================================ */
IOReturn mlx5_cmd_exec(struct mlx5_cmd_context *ctx,
                        uint16_t opcode, uint16_t op_mod,
                        const void *in_data, uint32_t in_len,
                        void *out_data, uint32_t out_len,
                        uint8_t *out_status)
{
    if (out_status) *out_status = 0xFF;

    /* Find a free slot (slot 0 is always used for simple commands) */
    uint32_t slot = 0;
    uint8_t token = ++ctx->next_token;
    if (token == 0) token = ++ctx->next_token; /* skip 0 */

    MLX5_LOG("cmd_exec: opcode=0x%x op_mod=0x%x slot=%u token=%u",
             opcode, op_mod, slot, token);

    /* Get the ring entry */
    volatile struct mlx5_cmd_prot_block *entry =
        (volatile struct mlx5_cmd_prot_block *)
        ((uint8_t *)ctx->cmd_ring + slot * ctx->entry_stride);

    /* Get input and output mailboxes for this slot */
    uint8_t *in_mbox  = ctx->mbox_pool + (slot * 2) * MLX5_CMD_MAILBOX_SIZE;
    uint8_t *out_mbox = ctx->mbox_pool + (slot * 2 + 1) * MLX5_CMD_MAILBOX_SIZE;
    uint64_t in_dma   = ctx->mbox_dma_base + (slot * 2) * MLX5_CMD_MAILBOX_SIZE;
    uint64_t out_dma  = ctx->mbox_dma_base + (slot * 2 + 1) * MLX5_CMD_MAILBOX_SIZE;

    /* Clear mailboxes */
    memset(in_mbox, 0, MLX5_CMD_MAILBOX_SIZE);
    memset(out_mbox, 0, MLX5_CMD_MAILBOX_SIZE);

    /* Fill input mailbox header (big-endian) */
    *(uint16_t *)(in_mbox + MLX5_MBOX_OPCODE_OFF) = OSSwapInt16(opcode);
    *(uint16_t *)(in_mbox + MLX5_MBOX_UID_OFF)    = OSSwapInt16(MLX5_CMD_UID_DRIVER);
    *(uint16_t *)(in_mbox + MLX5_MBOX_OP_MOD_OFF) = OSSwapInt16(op_mod);

    /* Copy input data (after the 16-byte header) */
    if (in_data && in_len > 0) {
        uint32_t copy_len = in_len;
        if (copy_len > MLX5_CMD_MAILBOX_SIZE - 16)
            copy_len = MLX5_CMD_MAILBOX_SIZE - 16;
        memcpy(in_mbox + 16, in_data, copy_len);
    }

    /* Fill the ring entry */
    entry->in_ptr  = OSSwapInt64(in_dma);
    entry->in_len  = OSSwapInt32(MLX5_CMD_MAILBOX_SIZE);
    entry->out_ptr = OSSwapInt64(out_dma);
    entry->out_len = OSSwapInt32(out_len > 0 ? out_len : MLX5_CMD_MAILBOX_SIZE);
    entry->token   = OSSwapInt32(token);
    entry->signature = 0; /* no signature for now */

    /* Memory barrier before submitting */
    OSSynchronizeIO();

    /* Set owner to HW and ring doorbell */
    entry->status = MLX5_CMD_OWNER_HW;
    OSSynchronizeIO();

    iseg_write32(ctx->iseg, MLX5_ISEG_CMD_DOORBELL, 1U << slot);

    MLX5_LOG("Doorbell rung for slot %u", slot);

    /* Poll for completion: wait for owner to flip back to SW */
    uint8_t delivery_status = 0;
    for (uint32_t i = 0; i < MLX5_CMD_TIMEOUT_MS / 10; i++) {
        OSSynchronizeIO();
        uint8_t owner = entry->status & 0x1;
        if (owner == MLX5_CMD_OWNER_SW) {
            delivery_status = (entry->status >> 1) & 0x7F;
            break;
        }
        IOSleep(10);
    }

    /* Check if we timed out */
    if ((entry->status & 0x1) != MLX5_CMD_OWNER_SW) {
        MLX5_LOG("ERROR: command 0x%x timed out", opcode);
        ctx->failed_commands++;
        return kIOReturnTimeout;
    }

    ctx->total_commands++;

    if (delivery_status != MLX5_CMD_DELIVERY_STAT_OK) {
        MLX5_LOG("ERROR: command 0x%x delivery status=0x%x", opcode, delivery_status);
        ctx->failed_commands++;
        return kIOReturnError;
    }

    /* Read response from output mailbox */
    uint8_t  fw_status  = out_mbox[MLX5_MBOX_STATUS_OFF];
    uint32_t fw_syndrome = OSSwapInt32(*(uint32_t *)(out_mbox + MLX5_MBOX_SYNDROME_OFF));

    if (out_status) *out_status = fw_status;

    if (fw_status != MLX5_CMD_STAT_OK) {
        MLX5_LOG("ERROR: command 0x%x fw_status=0x%x syndrome=0x%x",
                 opcode, fw_status, fw_syndrome);
        ctx->failed_commands++;
        return kIOReturnError;
    }

    /* Copy response data (after the 8-byte header) */
    if (out_data && out_len > 0) {
        uint32_t copy_len = out_len;
        if (copy_len > MLX5_CMD_MAILBOX_SIZE - 8)
            copy_len = MLX5_CMD_MAILBOX_SIZE - 8;
        memcpy(out_data, out_mbox + 8, copy_len);
    }

    MLX5_LOG("command 0x%x succeeded (fw_status=0)", opcode);
    return kIOReturnSuccess;
}

/* ============================================================
 * mlx5_cmd_cleanup — Free DMA resources
 * ============================================================ */
void mlx5_cmd_cleanup(struct mlx5_cmd_context *ctx)
{
    MLX5_LOG("cleanup: %u commands sent, %u failed",
             ctx->total_commands, ctx->failed_commands);

    if (ctx->mbox_md) {
        ctx->mbox_md->release();
        ctx->mbox_md = nullptr;
    }
    if (ctx->cmd_ring_md) {
        ctx->cmd_ring_md->release();
        ctx->cmd_ring_md = nullptr;
    }
}
