/*
 * mlx5_qp.cpp — Queue Pair creation, modification, and data path.
 *
 * This is the core of the RDMA data path. A QP has:
 *   - Send Queue (SQ): ring of WQEs that describe RDMA operations
 *   - Recv Queue (RQ): ring of WQEs that describe receive buffers
 *   - Doorbell Record: shared head/tail counters
 *   - UAR (BlueFlame): memory-mapped register for ringing doorbells
 *
 * The data path flow for RDMA WRITE:
 *   1. Application calls ibv_post_send(qp, wr)
 *   2. Driver writes a WQE to the SQ ring (DMA memory)
 *   3. Driver writes the WQE to the UAR BlueFlame register
 *   4. Firmware reads the WQE, performs the RDMA WRITE
 *   5. Firmware writes a CQE to the associated CQ
 *   6. Application polls the CQ for completion
 */

#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/OSByteOrder.h>

#include "mlx5_registers.h"
#include "mlx5_cmd.h"
#include "mlx5_qp.h"

#define MLX5_QP_LOG(fmt, ...) do { IOLog("mlx5_qp: " fmt "\n", ##__VA_ARGS__); } while(0)

/* ============================================================
 * alloc_qp_resources — Allocate DMA rings for SQ, RQ, and doorbell record
 * ============================================================ */
static IOReturn alloc_qp_resources(struct mlx5_ib_qp *qp,
                                   uint32_t sq_wqe_cnt, uint32_t sq_wqe_shift,
                                   uint32_t rq_wqe_cnt, uint32_t rq_wqe_shift)
{
    /* Send Queue ring */
    uint32_t sq_size = sq_wqe_cnt << sq_wqe_shift;
    qp->sq_md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut,
        sq_size, 0xFFFFFFFFFFFFF000ULL);
    if (!qp->sq_md) { MLX5_QP_LOG("SQ alloc failed (%u bytes)", sq_size); return kIOReturnNoMemory; }
    memset(qp->sq_md->getBytesNoCopy(), 0, sq_size);
    qp->sq_buf = qp->sq_md->getBytesNoCopy();
    qp->sq_dma = qp->sq_md->getPhysicalAddress();
    qp->sq_wqe_cnt = sq_wqe_cnt;
    qp->sq_wqe_shift = sq_wqe_shift;
    qp->sq_head = 0;

    /* Recv Queue ring */
    uint32_t rq_size = rq_wqe_cnt << rq_wqe_shift;
    qp->rq_md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut,
        rq_size, 0xFFFFFFFFFFFFF000ULL);
    if (!qp->rq_md) { MLX5_QP_LOG("RQ alloc failed (%u bytes)", rq_size);
        qp->sq_md->release(); return kIOReturnNoMemory; }
    memset(qp->rq_md->getBytesNoCopy(), 0, rq_size);
    qp->rq_buf = qp->rq_md->getBytesNoCopy();
    qp->rq_dma = qp->rq_md->getPhysicalAddress();
    qp->rq_wqe_cnt = rq_wqe_cnt;
    qp->rq_wqe_shift = rq_wqe_shift;
    qp->rq_head = 0;

    /* Doorbell Record (64 bytes minimum, we use first 8) */
    qp->db_md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut,
        4096, 0xFFFFFFFFFFFFF000ULL);
    if (!qp->db_md) { MLX5_QP_LOG("DB alloc failed");
        qp->sq_md->release(); qp->rq_md->release(); return kIOReturnNoMemory; }
    memset(qp->db_md->getBytesNoCopy(), 0, 4096);
    qp->db_record = (volatile uint32_t *)qp->db_md->getBytesNoCopy();
    qp->db_dma = qp->db_md->getPhysicalAddress();

    MLX5_QP_LOG("QP resources: SQ %u x %uB @0x%llx, RQ %u x %uB @0x%llx, DB @0x%llx",
                sq_wqe_cnt, 1 << sq_wqe_shift, qp->sq_dma,
                rq_wqe_cnt, 1 << rq_wqe_shift, qp->rq_dma, qp->db_dma);
    return kIOReturnSuccess;
}

/* ============================================================
 * mlx5_ib_create_qp_full — Create a QP via firmware CREATE_QP command
 * ============================================================ */
int mlx5_ib_create_qp_full(struct ib_qp *qp, struct ib_qp_init_attr *init,
                           struct mlx5_cmd_context *cmd_ctx)
{
    MLX5_QP_LOG("create_qp: type=%d max_send_wr=%d max_recv_wr=%d",
                init->qp_type, 0, 0); /* init->cap fields are simplified */

    struct mlx5_ib_qp *mqp = to_mqp(qp);
    memset(mqp, 0, sizeof(*mqp));

    /* Default queue sizes */
    uint32_t sq_wqe_cnt = 256;   /* 256 WQEs in send queue */
    uint32_t sq_wqe_shift = MLX5_SEND_WQE_SHIFT;  /* 6 = 64-byte WQEs */
    uint32_t rq_wqe_cnt = 256;   /* 256 WQEs in recv queue */
    uint32_t rq_wqe_shift = MLX5_RECV_WQE_SHIFT;   /* 4 = 16-byte WQEs */

    /* Allocate DMA rings */
    IOReturn ret = alloc_qp_resources(mqp, sq_wqe_cnt, sq_wqe_shift,
                                      rq_wqe_cnt, rq_wqe_shift);
    if (ret != kIOReturnSuccess) return -1;

    /* Build CREATE_QP command input.
     * Layout: [cmd_header(16)] [qpc(~500 bytes)] [pas[]]
     * We simplify the QPC to the essential fields. */
    uint8_t in_buf[1024] = {0};
    uint8_t out_buf[64]  = {0};

    uint8_t *qpc = in_buf;  /* QPC starts at beginning of data area */

    /* log_rq_size: bits [27:24] at QPC offset 0x04 */
    uint32_t log_rq_size = 0;
    uint32_t tmp = rq_wqe_cnt;
    while (tmp > 1) { log_rq_size++; tmp >>= 1; }
    qpc[0x04] |= (log_rq_size & 0x0F) << 4;

    /* log_rq_stride: bits [23:20] at QPC offset 0x04 (4 = 16B WQEs) */
    uint32_t log_rq_stride = rq_wqe_shift - 4;
    qpc[0x04] |= (log_rq_stride & 0x0F) << 0;  /* simplified */

    /* log_sq_size: bits [27:24] at QPC offset 0x0C */
    uint32_t log_sq_size = 0;
    tmp = sq_wqe_cnt;
    while (tmp > 1) { log_sq_size++; tmp >>= 1; }
    qpc[0x0C] |= (log_sq_size & 0x0F) << 4;

    /* cqn_snd at QPC offset 0x30 */
    /* cqn_rcv at QPC offset 0x34 */
    /* These come from the associated CQs */
    /* For now, use CQ numbers from init */
    if (init->send_cq) mqp->send_cqn = init->send_cq->cqe;
    if (init->recv_cq) mqp->recv_cqn = init->recv_cq->cqe;

    /* PD number */
    if (qp->pd) mqp->pdn = qp->pd->local_dma_lkey;

    /* Doorbell record address at QPC offset for dbr_addr */
    *(uint64_t *)(qpc + 0x100) = OSSwapInt64(mqp->db_dma);

    /* Physical addresses of SQ and RQ rings (pas array after QPC) */
    *(uint64_t *)(in_buf + 0x200) = OSSwapInt64(mqp->sq_dma);
    *(uint64_t *)(in_buf + 0x208) = OSSwapInt64(mqp->rq_dma);

    /* Send CREATE_QP command */
    uint8_t fw_status = 0xFF;
    ret = mlx5_cmd_exec(cmd_ctx,
                        MLX5_CMD_OP_CREATE_QP, 0,
                        in_buf, 0x210,
                        out_buf, sizeof(out_buf),
                        &fw_status);
    if (ret != kIOReturnSuccess) {
        MLX5_QP_LOG("CREATE_QP failed: fw_status=0x%02x", fw_status);
        mqp->sq_md->release(); mqp->rq_md->release(); mqp->db_md->release();
        return -1;
    }

    /* Extract QP number from response */
    mqp->qpn = OSSwapInt32(*(uint32_t *)(out_buf + 8));
    qp->qp_num = mqp->qpn;
    qp->state = IB_QPS_RESET;
    mqp->state = IB_QPS_RESET;

    MLX5_QP_LOG("QP created: qpn=%d", mqp->qpn);
    return 0;
}

/* ============================================================
 * mlx5_ib_modify_qp_full — QP state machine via MODIFY_QP command
 *
 * State transitions for RC (Reliable Connection) QPs:
 *   RESET → INIT: set port, pkey_index, access_flags
 *   INIT → RTR: set remote QPN, remote GID, MTU, min_rnr_timer
 *   RTR → RTS: set timeout, retry_cnt, rnr_retry, sq_psn
 * ============================================================ */
int mlx5_ib_modify_qp_full(struct ib_qp *qp, int attr_mask,
                           void *attr, struct mlx5_cmd_context *cmd_ctx)
{
    struct mlx5_ib_qp *mqp = to_mqp(qp);

    /* Determine target state based on current state + attr_mask */
    uint32_t target_state;
    switch (mqp->state) {
        case IB_QPS_RESET:
            target_state = MLX5_QPC_QST_INIT;
            break;
        case IB_QPS_INIT:
            target_state = MLX5_QPC_QST_RTR;
            break;
        case IB_QPS_RTR:
            target_state = MLX5_QPC_QST_RTS;
            break;
        default:
            MLX5_QP_LOG("modify_qp: unexpected state %d", mqp->state);
            return -1;
    }

    uint8_t in_buf[256] = {0};
    uint8_t out_buf[16] = {0};

    /* Set QP state in QPC */
    in_buf[0x00] = (target_state << 4);  /* cur_state [31:28] */
    in_buf[0x00] |= (target_state);      /* next_state [27:24] */

    uint8_t fw_status = 0xFF;
    IOReturn ret = mlx5_cmd_exec(cmd_ctx,
                                 MLX5_CMD_OP_MODIFY_QP, 0,
                                 in_buf, 0x100,
                                 out_buf, sizeof(out_buf),
                                 &fw_status);
    if (ret != kIOReturnSuccess) {
        MLX5_QP_LOG("MODIFY_QP failed: fw_status=0x%02x state=%d->%d",
                    fw_status, mqp->state, target_state);
        return -1;
    }

    /* Update state tracking */
    switch (target_state) {
        case MLX5_QPC_QST_INIT: mqp->state = IB_QPS_INIT; qp->state = IB_QPS_INIT; break;
        case MLX5_QPC_QST_RTR:  mqp->state = IB_QPS_RTR;  qp->state = IB_QPS_RTR;  break;
        case MLX5_QPC_QST_RTS:  mqp->state = IB_QPS_RTS;  qp->state = IB_QPS_RTS;  break;
    }

    MLX5_QP_LOG("QP %d state -> %d", mqp->qpn, target_state);
    return 0;
}

/* ============================================================
 * mlx5_ib_destroy_qp_full — Destroy QP and free DMA rings
 * ============================================================ */
int mlx5_ib_destroy_qp_full(struct ib_qp *qp,
                            struct mlx5_cmd_context *cmd_ctx)
{
    struct mlx5_ib_qp *mqp = to_mqp(qp);

    uint8_t in_buf[16] = {0};
    uint8_t out_buf[16] = {0};
    *(uint32_t *)(in_buf) = OSSwapInt32(mqp->qpn);

    uint8_t fw_status = 0xFF;
    IOReturn ret = mlx5_cmd_exec(cmd_ctx,
                                 MLX5_CMD_OP_DESTROY_QP, 0,
                                 in_buf, 4,
                                 out_buf, sizeof(out_buf),
                                 &fw_status);
    if (ret == kIOReturnSuccess) {
        MLX5_QP_LOG("QP %d destroyed", mqp->qpn);
    }

    /* Free DMA rings */
    if (mqp->sq_md) mqp->sq_md->release();
    if (mqp->rq_md) mqp->rq_md->release();
    if (mqp->db_md) mqp->db_md->release();

    return (ret == kIOReturnSuccess) ? 0 : -1;
}

/* ============================================================
 * mlx5_ib_post_send_full — Write a WQE to the SQ and ring the doorbell
 *
 * This is the RDMA data path. Called from ib_post_send.
 * ============================================================ */
int mlx5_ib_post_send_full(struct ib_qp *qp, const void *wr,
                           const void **bad_wr)
{
    struct mlx5_ib_qp *mqp = to_mqp(qp);

    /* Get the next WQE slot in the SQ ring */
    uint32_t head = mqp->sq_head;
    uint32_t wqe_offset = head << mqp->sq_wqe_shift;

    /* Write the control segment of the WQE */
    struct mlx5_wqe_ctrl_seg *ctrl =
        (struct mlx5_wqe_ctrl_seg *)((uint8_t *)mqp->sq_buf + wqe_offset);

    /* For RDMA WRITE (opcode 0x08): */
    ctrl->opmod_idx_opcode = OSSwapInt32(
        (MLX5_OPCODE_RDMA_WRITE << 24) |  /* opcode */
        (0 << 16) |                        /* opmod */
        (head & 0xFFFF)                    /* WQE index */
    );

    /* qpn + data segment count (1 data segment for simple write) */
    ctrl->qpn_ds = OSSwapInt32(
        ((mqp->qpn & 0xFFFFFF) << 8) | 2  /* 2 = ctrl + 1 data seg */
    );

    /* Signal CQ on completion */
    ctrl->flags = OSSwapInt32(1 << 31);  /* CQE generation */
    ctrl->fm_ce_se = 0;

    /* The caller's WR contains the RDMA WRITE parameters:
     * - remote addr + rkey (target buffer on the remote node)
     * - local lkey + addr (source buffer on this node)
     * In the full implementation, we parse the ib_send_wr and fill
     * the RDMA segment + data segment. For now, this shows the WQE
     * structure that gets written to the SQ ring. */

    /* Update SQ head */
    mqp->sq_head = (head + 1) & (mqp->sq_wqe_cnt - 1);

    /* Update doorbell record */
    mqp->db_record[0] = OSSwapInt32(mqp->sq_head << mqp->sq_wqe_shift);

    /* Ring BlueFlame doorbell in UAR.
     * The UAR is memory-mapped; writing to it notifies firmware.
     * For kernel driver: write the WQE to UAR followed by a doorbell.
     *
     * NOTE: In kernel mode, we write directly to the UAR virtual address.
     * The UAR region is mapped via IOMapDeviceMemory from the PCI BAR
     * or from a dedicated UAR BAR.
     */
    if (mqp->uar) {
        volatile uint64_t *bf = (volatile uint64_t *)mqp->uar;
        /* Write the first 8 bytes of the WQE to BlueFlame */
        uint64_t wqe_first8 = *(uint64_t *)ctrl;
        *bf = OSSwapInt64(wqe_first8);
        OSSynchronizeIO();
    }

    /* Memory barrier */
    OSSynchronizeIO();

    MLX5_QP_LOG("post_send: qpn=%d WQE[%d] written", mqp->qpn, head);
    return 0;
}

/* ============================================================
 * mlx5_ib_post_recv_full — Post a receive buffer to the RQ
 * ============================================================ */
int mlx5_ib_post_recv_full(struct ib_qp *qp, const void *wr,
                           const void **bad_wr)
{
    struct mlx5_ib_qp *mqp = to_mqp(qp);

    uint32_t head = mqp->rq_head;
    uint32_t wqe_offset = head << mqp->rq_wqe_shift;

    /* RQ WQE is a data segment: byte_count + lkey + addr */
    struct mlx5_wqe_data_seg *dseg =
        (struct mlx5_wqe_data_seg *)((uint8_t *)mqp->rq_buf + wqe_offset);

    /* The caller's WR contains the receive buffer info.
     * In the full implementation, we extract:
     *   byte_count = wr->sg_list[0].length
     *   lkey = wr->sg_list[0].lkey
     *   addr = wr->sg_list[0].addr
     * For now, set placeholder values. */
    dseg->byte_count = OSSwapInt32(4096);  /* 4KB receive buffer */
    dseg->lkey = 0;                         /* needs MR lkey */
    dseg->addr = 0;                         /* needs DMA addr */

    /* Update RQ head */
    mqp->rq_head = (head + 1) & (mqp->rq_wqe_cnt - 1);

    /* Update doorbell record */
    mqp->db_record[1] = OSSwapInt32(mqp->rq_head & 0xFFFF);

    OSSynchronizeIO();

    MLX5_QP_LOG("post_recv: qpn=%d RQE[%d] posted", mqp->qpn, head);
    return 0;
}
