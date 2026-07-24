/*
 * mlx5_cmd.h — Command queue interface for the mlx5 HCA.
 *
 * The command interface is a ring of mailboxes in BAR0 that
 * communicate with the firmware. Each command is submitted by
 * writing to a mailbox and then writing the command token to
 * the command doorbell.
 */

#ifndef MLX5_CMD_H
#define MLX5_CMD_H

#include "mlx5_registers.h"
#include <stdint.h>

/* ============================================================
 * mlx5_cmd mailbox — A single command/response entry
 *
 * Each mailbox is 16 bytes in the inline ring, plus an optional
 * DMA-mapped data block for large commands.
 * ============================================================ */
struct mlx5_cmd_mailbox
{
    void   *buf;                 // DMA-mapped data block
    uint32_t dma_addr_lo;        // Low 32 bits of DMA address
    uint32_t dma_addr_hi;        // High 32 bits of DMA address
    struct  mlx5_cmd_mailbox *next;  // Linked list for multi-block
};

/* ============================================================
 * mlx5_cmd_block — The inline 16-byte command header
 *
 * This is written to the command queue ring in BAR0.
 * ============================================================ */
struct mlx5_cmd_block
{
    uint32_t  type;              // [0:3]   = opcode
                                 // [4:7]   = opcode_mod
                                 // [8:11]  = input_hout
                                 // [12:15] = token
    uint32_t  input_length;      // [16:19] = input data length
    uint32_t  output_length;     // [20:23] = output data length
    uint32_t  status;            // [24:27] = completion status (fw writes)
    uint32_t  syndrome;          // [28:31] = error syndrome
    uint32_t  inline_data[4];    // [32:47] = inline command data
    uint64_t  mailbox_ptr;       // [48:55] = pointer to data mailbox
    uint32_t  token;             // [56:59] = command token
    uint32_t  signature;         // [60:63] = signature
};

/* Command mode flags */
#define MLX5_CMD_MODE_POLLING    0   // Poll for completion
#define MLX5_CMD_MODE_EVENTS     1   // Wait for event interrupt

/* ============================================================
 * mlx5_cmd_stats — Statistics for the command interface
 * ============================================================ */
struct mlx5_cmd_stats
{
    uint32_t  total_commands;
    uint32_t  failed_commands;
    uint32_t  timeout_count;
};

/* ============================================================
 * API functions (to be implemented in mlx5_cmd.cpp)
 * ============================================================ */

// Initialize the command queue ring
int mlx5_cmd_init(struct mlx5_cmd_context *ctx);

// Send a command and wait for completion (polling mode)
int mlx5_cmd_exec_sync(struct mlx5_cmd_context *ctx,
                       uint16_t opcode,
                       uint16_t opcode_mod,
                       const void *in_buf,  uint32_t in_len,
                       void *out_buf,        uint32_t out_len);

// Send a command asynchronously
int mlx5_cmd_exec_async(struct mlx5_cmd_context *ctx,
                        uint16_t opcode,
                        uint16_t opcode_mod,
                        const void *in_buf,  uint32_t in_len,
                        void *out_buf,        uint32_t out_len);

// Poll for command completion
int mlx5_cmd_poll(struct mlx5_cmd_context *ctx, uint32_t timeout_ms);

// Allocate a data mailbox (DMA-mapped)
struct mlx5_cmd_mailbox *mlx5_alloc_mailbox(void);
void mlx5_free_mailbox(struct mlx5_cmd_mailbox *mb);

#endif /* MLX5_CMD_H */
