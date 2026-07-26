#ifndef MLX5_QP_H
#define MLX5_QP_H

/*
 * mlx5_qp.h — Queue Pair structures and WQE layouts.
 *
 * A QP has two work queues: Send Queue (SQ) and Recv Queue (RQ).
 * Each is a ring of Work Queue Elements (WQEs) in DMA memory.
 * The driver writes WQEs and rings a doorbell; the firmware reads
 * them and processes the data path.
 */

#include "mlx5_cmd.h"
#include "rdma/ib_verbs.h"

/* ============================================================
 * WQE (Work Queue Element) sizes
 * ============================================================ */
#define MLX5_SEND_WQE_BB     64    /* One WQE Basic Block = 64 bytes */
#define MLX5_SEND_WQE_SHIFT  6     /* log2(64) */
#define MLX5_RECV_WQE_SHIFT  4     /* Default RQ WQE is 16 bytes (pointer) */

/* ============================================================
 * mlx5_ib_qp — Provider-private QP data
 * ============================================================ */
struct mlx5_ib_qp {
    struct ib_qp        ibqp;
    uint32_t            qpn;           /* QP number from firmware */

    /* Send Queue */
    void               *sq_buf;        /* DMA ring for SQ WQEs */
    IOBufferMemoryDescriptor *sq_md;
    uint64_t            sq_dma;
    uint32_t            sq_wqe_cnt;    /* Number of WQEs in SQ */
    uint32_t            sq_wqe_shift;  /* log2(WQE size) */
    uint32_t            sq_head;       /* Next WQE to write */

    /* Recv Queue */
    void               *rq_buf;        /* DMA ring for RQ WQEs */
    IOBufferMemoryDescriptor *rq_md;
    uint64_t            rq_dma;
    uint32_t            rq_wqe_cnt;
    uint32_t            rq_wqe_shift;
    uint32_t            rq_head;

    /* Doorbell Record (shared SQ/RQ head/tail counters) */
    volatile uint32_t  *db_record;     /* DMA-mapped doorbell record */
    IOBufferMemoryDescriptor *db_md;
    uint64_t            db_dma;

    /* UAR (User Access Region) for BlueFlame doorbell */
    volatile void      *uar;           /* UAR virtual address */

    /* Associated CQs */
    uint32_t            send_cqn;
    uint32_t            recv_cqn;
    uint32_t            pdn;           /* PD number */

    enum ib_qp_state    state;
};

static inline struct mlx5_ib_qp *to_mqp(struct ib_qp *ibqp)
{
    return (struct mlx5_ib_qp *)ibqp;
}

/* ============================================================
 * QP Context (QPC) — The ~500-byte structure sent in CREATE_QP
 *
 * Key fields and their byte offsets within the QPC:
 * ============================================================ */
#define MLX5_QPC_OFF_STATE        0x00    /* bits [31:28] = QP state */
#define MLX5_QPC_OFF_ST           0x00    /* bits [27:24] = next state */
#define MLX5_QPC_OFF_PD           0x0C    /* [31:0] = PD number */
#define MLX5_QPC_OFF_CQN_SND      0x30    /* [23:0] = send CQN */
#define MLX5_QPC_OFF_CQN_RCV      0x34    /* [23:0] = recv CQN */
#define MLX5_QPC_OFF_LOG_RQ_SIZE  0x04    /* bits [27:24] */
#define MLX5_QPC_OFF_LOG_RQ_STRIDE 0x04   /* bits [23:20] */
#define MLX5_QPC_OFF_LOG_SQ_SIZE  0x0C    /* bits [27:24] */
#define MLX5_QPC_OFF_LOG_SQ_STRIDE 0x0C   /* bits [23:20] */

/* QP states (firmware enum) */
#define MLX5_QPC_QST_RST     0
#define MLX5_QPC_QST_INIT    1
#define MLX5_QPC_QST_RTR     2
#define MLX5_QPC_QST_RTS     3
#define MLX5_QPC_QST_SQER    4
#define MLX5_QPC_QST_SQD     5
#define MLX5_QPC_QST_ERR     6

/* ============================================================
 * Send Queue WQE Layout (64-byte Basic Block)
 *
 * A WQE consists of:
 *   [0:15]   = Control segment (opcode, size, flags)
 *   [16:31]  = Ethernet segment (for RoCEv2)
 *   [32:...] = Data segments (scatter/gather)
 * ============================================================ */
struct mlx5_wqe_ctrl_seg {
    __be32  opmod_idx_opcode;   /* [31:24]=opcode, [23:16]=opmod, [15:0]=index */
    __be32  qpn_ds;             /* [31:8]=qpn, [7:0]=data_seg_count */
    __be32  flags;              /* [31]=fm_ce_se, [15:12]=cf, etc. */
    __be32  fm_ce_se;           /* signal/flush/imm flags */
};

#define MLX5_OPCODE_RDMA_WRITE       0x08
#define MLX5_OPCODE_SEND             0x0A
#define MLX5_OPCODE_RDMA_READ        0x09

/* Data segment (scatter/gather entry) */
struct mlx5_wqe_data_seg {
    __be32  byte_count;         /* data length */
    __be32  lkey;               /* MR local key */
    __be64  addr;               /* DMA address of the data buffer */
};

/* ============================================================
 * API
 * ============================================================ */

/* Full QP creation (replaces the stub in mlx5_verbs.cpp) */
int mlx5_ib_create_qp_full(struct ib_qp *qp, struct ib_qp_init_attr *init,
                           struct mlx5_cmd_context *cmd_ctx);
int mlx5_ib_modify_qp_full(struct ib_qp *qp, int attr_mask,
                           void *attr, struct mlx5_cmd_context *cmd_ctx);
int mlx5_ib_destroy_qp_full(struct ib_qp *qp,
                            struct mlx5_cmd_context *cmd_ctx);
int mlx5_ib_post_send_full(struct ib_qp *qp, const void *wr,
                           const void **bad_wr);
int mlx5_ib_post_recv_full(struct ib_qp *qp, const void *wr,
                           const void **bad_wr);

#endif /* MLX5_QP_H */
