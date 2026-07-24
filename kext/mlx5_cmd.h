#ifndef MLX5_CMD_H
#define MLX5_CMD_H

#include "mlx5_registers.h"
#include <stdint.h>

/* ============================================================
 * mlx5_cmd_prot_block — Command ring entry (64 bytes)
 * Located in DMA-allocated command ring memory.
 * ============================================================ */
struct mlx5_cmd_prot_block
{
    uint8_t   status;           /* [0]     owner bit (bit0) + delivery status */
    uint8_t   reserved0;        /* [1] */
    uint16_t  reserved1;        /* [2-3] */
    uint64_t  in_ptr;           /* [4-11]  DMA ptr to input mailbox */
    uint32_t  in_len;           /* [12-15] input data length */
    uint64_t  out_ptr;          /* [16-23] DMA ptr to output mailbox */
    uint32_t  out_len;          /* [24-27] output max length */
    uint32_t  token;            /* [28-31] command token */
    uint8_t   signature;        /* [32]    signature */
    uint8_t   reserved2[7];     /* [33-39] */
    uint8_t   reserved3[24];    /* [40-63] */
} __attribute__((packed));

/* ============================================================
 * mlx5_cmd_context — Per-device command queue state
 * ============================================================ */
struct mlx5_cmd_context
{
    /* BAR0 virtual address (mapped by the kext) */
    volatile uint8_t *bar0;
    uint32_t          bar0_size;

    /* Init segment virtual address (bar0 + 0x1000) */
    volatile uint8_t *iseg;

    /* Command ring DMA memory */
    volatile struct mlx5_cmd_prot_block *cmd_ring;
    IOBufferMemoryDescriptor  *cmd_ring_md;
    uint64_t                   cmd_ring_dma;
    uint32_t                   num_entries;
    uint32_t                   entry_stride;

    /* Mailbox pool (input + output) */
    uint8_t              *mbox_pool;    /* array of mailbox buffers */
    IOBufferMemoryDescriptor *mbox_md;
    uint64_t               mbox_dma_base;
    uint32_t               num_mailboxes;

    /* Command token counter */
    volatile uint8_t      next_token;

    /* Statistics */
    uint32_t              total_commands;
    uint32_t              failed_commands;
};

/* ============================================================
 * API functions (implemented in mlx5_cmd.cpp)
 * ============================================================ */

/* Initialize the command queue: allocate DMA ring + mailboxes,
 * register with firmware via init segment registers */
IOReturn mlx5_cmd_init(struct mlx5_cmd_context *ctx,
                       volatile uint8_t *bar0, uint32_t bar0_size);

/* Execute a command synchronously (blocking, polling mode).
 * Returns kIOReturnSuccess on firmware-level success.
 * out_status gets the firmware status byte (0=success). */
IOReturn mlx5_cmd_exec(struct mlx5_cmd_context *ctx,
                        uint16_t opcode, uint16_t op_mod,
                        const void *in_data, uint32_t in_len,
                        void *out_data, uint32_t out_len,
                        uint8_t *out_status);

/* Cleanup: free DMA ring + mailboxes */
void mlx5_cmd_cleanup(struct mlx5_cmd_context *ctx);

#endif /* MLX5_CMD_H */
