#ifndef MLX5_MR_H
#define MLX5_MR_H

/*
 * mlx5_mr.h — Memory Registration structures.
 *
 * Memory registration maps userspace buffers for RDMA access:
 *   1. Pin the user's pages in physical memory
 *   2. Get the DMA addresses of each page
 *   3. Build an MTT (Memory Translation Table) entry
 *   4. Send CREATE_MKEY command to firmware
 *   5. Firmware returns lkey/rkey for referencing this memory
 */

#include "mlx5_cmd.h"
#include "rdma/ib_verbs.h"

/* ============================================================
 * mlx5_ib_mr — Provider-private memory region data
 * ============================================================ */
struct mlx5_ib_mr {
    struct ib_mr  ibmr;
    uint32_t      mkey;       /* Firmware-assigned MKey index */
    uint32_t      mtt_level;  /* MTT indirection level */

    /* DMA-mapped page list */
    uint64_t     *pas;        /* Physical address array */
    uint32_t      npages;     /* Number of pages */
    IOBufferMemoryDescriptor *pas_md;
};

static inline struct mlx5_ib_mr *to_mmr(struct ib_mr *ibmr)
{
    return (struct mlx5_ib_mr *)ibmr;
}

/* ============================================================
 * CREATE_MKEY command layout (key fields in the MKC)
 *
 * The Memory Key Context (MKC) is ~100 bytes:
 *   offset 0x00: access_mode (bits [31:29])
 *     1 = MTT (standard), 2 = KLMs, 3 = KSMs
 *   offset 0x04: a (access flags)
 *   offset 0x08: len (64-bit memory region length)
   *   offset 0x10: start_addr (64-bit I/O virtual address)
 *   offset 0x18: log_entity_size_55_48, etc.
 *   offset 0x20: bsf_id, log_page_size
 *   offset 0x40: pd (PD number, 24-bit)
 *   offset 0x48: len_63_32 (high 32 bits of length)
 *   offset 0x50: start_addr_63_32
 * ============================================================ */
#define MLX5_MKC_OFF_ACCESS_MODE  0x00
#define MLX5_MKC_ACCESS_MODE_MTT  1
#define MLX5_MKC_OFF_A_FLAGS      0x04
#define MLX5_MKC_OFF_LEN          0x08
#define MLX5_MKC_OFF_IOVA         0x10
#define MLX5_MKC_OFF_PD           0x40
#define MLX5_MKC_OFF_LOG_PAGE_SIZE 0x58

/* ============================================================
 * API
 * ============================================================ */
struct ib_mr *mlx5_ib_reg_user_mr_full(struct ib_pd *pd, uint64_t start,
                                       uint64_t length, uint64_t virt_addr,
                                       int access_flags,
                                       struct mlx5_cmd_context *cmd_ctx);

#endif /* MLX5_MR_H */
