/*
 * mlx5_verbs.cpp — RDMA verbs implementations for mlx5.
 *
 * These functions implement the ib_device_ops interface by translating
 * standard RDMA verbs into mlx5 firmware commands.
 *
 * The flow for each verb:
 *   1. Application calls ibv_alloc_pd/ibv_create_cq/etc. (libibverbs)
 *   2. libibverbs sends command via mach IPC to IORDMAFamilyn *   3. IORDMAFamily calls ib_device_ops.alloc_pd/create_cq/etc.
 *   4. These functions send firmware commands via mlx5_cmd_exec
 *   5. Results are stored in the ib_pd/ib_cq/ib_qp struct
 *
 * Ported from Linux:
 *   drivers/infiniband/hw/mlx5/main.c — PD, device queries
 *   drivers/infiniband/hw/mlx5/cq.c   — CQ operations
 *   drivers/infiniband/hw/mlx5/qp.c   — QP operations
 *   drivers/infiniband/hw/mlx5/mr.c   — MR operations
 */

#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>

#include "mlx5_registers.h"
#include "mlx5_cmd.h"
#include "mlx5_verbs.h"

#define MLX5_VRB_LOG(fmt, args...) \
    IOLog("mlx5_verbs: " fmt "\n", ##args)

/* ============================================================
 * PD: Protection Domain
 *
 * alloc_pd: send CREATE_MKEY-less PD allocation via ALLOC_PD command.
 * In mlx5, a PD is just a 32-bit identifier assigned by firmware.
 * ============================================================ */

/* MLX5 opcode for alloc_pd (firmware command) */
#define MLX5_CMD_OP_ALLOC_PD    0x201  /* Actually allocate via MKEY variant */
#define MLX5_CMD_OP_DEALLOC_PD  0x203

int mlx5_ib_alloc_pd(struct ib_pd *pd, void *udata)
{
    /* ALLOC_PD input: just opcode (0xC120 in firmware cmd format).
     * Actually mlx5 uses ALLOC_PD which returns a pdn (PD number).
     *
     * Input mailbox (after header):
     *   empty (just opcode + op_mod)
     *
     * Output mailbox:
     *   offset 0: status
     *   offset 8: pdn (32-bit, big-endian)
     */
    struct mlx5_ib_dev *dev = to_mdev(pd->device);
    uint8_t out_buf[16] = {0};
    uint8_t fw_status = 0xFF;

    IOReturn ret = mlx5_cmd_exec(dev->cmd_ctx,
                                 MLX5_CMD_OP_CREATE_MKEY, 0x01,  /* op_mod=1 for PD */
                                 NULL, 0,
                                 out_buf, sizeof(out_buf),
                                 &fw_status);
    if (ret != kIOReturnSuccess) {
        MLX5_VRB_LOG("alloc_pd failed: fw_status=0x%02x", fw_status);
        return -1;
    }

    /* Extract PD number from response */
    uint32_t pdn = OSSwapInt32(*(uint32_t *)(out_buf + 8));

    /* Store pdn in the pd struct (we use the local_dma_lkey field
     * as a temporary location; in the real implementation we'd have
     * a mlx5_ib_pd private struct) */
    pd->local_dma_lkey = pdn;

    MLX5_VRB_LOG("alloc_pd: pdn=%d", pdn);
    return 0;
}

int mlx5_ib_dealloc_pd(struct ib_pd *pd, void *udata)
{
    struct mlx5_ib_dev *dev = to_mdev(pd->device);
    uint32_t pdn = pd->local_dma_lkey;

    uint8_t in_buf[16] = {0};
    uint8_t out_buf[16] = {0};
    uint8_t fw_status = 0xFF;

    /* DEALLOC_PD input: pdn at offset 8 */
    *(uint32_t *)(in_buf) = OSSwapInt32(pdn);

    IOReturn ret = mlx5_cmd_exec(dev->cmd_ctx,
                                 MLX5_CMD_OP_DESTROY_MKEY, 0x01,
                                 in_buf, 4,
                                 out_buf, sizeof(out_buf),
                                 &fw_status);
    if (ret != kIOReturnSuccess) {
        MLX5_VRB_LOG("dealloc_pd failed: fw_status=0x%02x", fw_status);
        return -1;
    }

    MLX5_VRB_LOG("dealloc_pd: pdn=%d", pdn);
    return 0;
}

/* ============================================================
 * CQ: Completion Queue
 *
 * create_cq: allocate DMA memory for the CQ ring, send CREATE_CQ
 * command with the DMA address. Firmware writes CQEs to this ring.
 * ============================================================ */

#define MLX5_CMD_OP_CREATE_CQ   0x400
#define MLX5_CMD_OP_DESTROY_CQ  0x401

int mlx5_ib_create_cq(struct ib_cq *cq, void *cq_attr, void *udata)
{
    struct mlx5_ib_dev *dev = to_mdev(cq->device);

    /* Allocate DMA memory for CQ entries (CQEs).
     * Each CQE is 64 bytes. We allocate cqe_count * 64 bytes. */
    uint32_t cqe_count = cq->cqe > 0 ? cq->cqe : 64;
    uint32_t cqe_size = 64;
    uint32_t ring_size = cqe_count * cqe_size;

    IOBufferMemoryDescriptor *cqMD =
        IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
            kernel_task,
            kIOMemoryPhysicallyContiguous | kIODirectionInOut,
            ring_size,
            0xFFFFFFFFFFFFF000ULL);
    if (!cqMD) {
        MLX5_VRB_LOG("create_cq: DMA alloc failed (%u bytes)", ring_size);
        return -1;
    }
    memset(cqMD->getBytesNoCopy(), 0, ring_size);

    /* Get DMA address */
    uint64_t cq_dma = cqMD->getPhysicalAddress();

    /* CREATE_CQ input (after mailbox header):
     *   offset 0x00: cqc (CQ context, ~64 bytes of bitfields)
     *     - cqe_size: bits within cqc
     *     - log_cq_size: log2(cqe_count)
     *     - uar_page, c_eqn, etc.
     *   offset 0x40: pas[0] (physical address of CQ ring)
     */
    uint8_t in_buf[256] = {0};
    uint8_t out_buf[16] = {0};

    /* Fill CQC (simplified - real impl has many more fields) */
    uint8_t log_cq_size = 0;
    uint32_t tmp = cqe_count;
    while (tmp > 1) { log_cq_size++; tmp >>= 1; }

    /* cqc starts at offset 0x00 in the command data */
    in_buf[0x0C] = log_cq_size;  /* log_cq_size */
    in_buf[0x0E] = 1;            /* cqe_size = 64B selector */

    /* pas[0] at offset 0x40 */
    *(uint64_t *)(in_buf + 0x40) = OSSwapInt64(cq_dma);

    uint8_t fw_status = 0xFF;
    IOReturn ret = mlx5_cmd_exec(dev->cmd_ctx,
                                 MLX5_CMD_OP_CREATE_CQ, 0,
                                 in_buf, 0x48,
                                 out_buf, sizeof(out_buf),
                                 &fw_status);
    if (ret != kIOReturnSuccess) {
        MLX5_VRB_LOG("create_cq failed: fw_status=0x%02x", fw_status);
        cqMD->release();
        return -1;
    }

    /* Extract cqn (CQ number) from response */
    uint32_t cqn = OSSwapInt32(*(uint32_t *)(out_buf + 8));

    /* Store CQ metadata (in real impl, a mlx5_ib_cq struct) */
    cq->cqe = cqn;  /* reuse field temporarily */

    MLX5_VRB_LOG("create_cq: cqn=%d, %u entries @ DMA 0x%llx",
                 cqn, cqe_count, cq_dma);
    return 0;
}

int mlx5_ib_destroy_cq(struct ib_cq *cq, void *udata)
{
    struct mlx5_ib_dev *dev = to_mdev(cq->device);
    uint32_t cqn = cq->cqe;

    uint8_t in_buf[16] = {0};
    uint8_t out_buf[16] = {0};

    /* DESTROY_CQ input: cqn at offset 8 */
    *(uint32_t *)(in_buf) = OSSwapInt32(cqn);

    uint8_t fw_status = 0xFF;
    IOReturn ret = mlx5_cmd_exec(dev->cmd_ctx,
                                 MLX5_CMD_OP_DESTROY_CQ, 0,
                                 in_buf, 4,
                                 out_buf, sizeof(out_buf),
                                 &fw_status);
    if (ret == kIOReturnSuccess) {
        MLX5_VRB_LOG("destroy_cq: cqn=%d", cqn);
    }
    return (ret == kIOReturnSuccess) ? 0 : -1;
}

int mlx5_ib_poll_cq(struct ib_cq *cq, int ne, struct ib_wc *wc)
{
    /* Polling reads CQEs from the DMA ring without sending a command.
     * The CQ ring is in DMA memory; we check the owner bit of each CQE
         * to know if firmware has written a new completion.
     *
     * For Phase 2 skeleton, this is a stub. The real implementation
     * needs the CQ ring virtual address stored in a mlx5_ib_cq struct.
     */
    MLX5_VRB_LOG("poll_cq: stub (needs CQ ring access)");
    return 0;  /* no completions */
}

int mlx5_ib_req_notify_cq(struct ib_cq *cq, int flags)
{
    /* Arms the CQ for interrupt notification.
     * In mlx5, this writes to the UAR (User Access Region) doorbell.
     */
    MLX5_VRB_LOG("req_notify_cq: stub (needs UAR doorbell)");
    return 0;
}

/* ============================================================
 * QP: Queue Pair (stubs for Phase 2)
 * ============================================================ */

int mlx5_ib_create_qp(struct ib_qp *qp, struct ib_qp_init_attr *init,
                      void *udata)
{
    /* CREATE_QP is the most complex command (~500 bytes of input).
     * It allocates send/recv queues, doorbell records, and a QP context.
     * Full implementation needs ~200 lines.
     */
    MLX5_VRB_LOG("create_qp: stub (needs QP context setup)");
    return -1;
}

int mlx5_ib_modify_qp(struct ib_qp *qp, int mask, void *attr, void *udata)
{
    MLX5_VRB_LOG("modify_qp: stub");
    return -1;
}

int mlx5_ib_destroy_qp(struct ib_qp *qp, void *udata)
{
    MLX5_VRB_LOG("destroy_qp: stub");
    return -1;
}

int mlx5_ib_post_send(struct ib_qp *qp, const void *wr, const void **bad_wr)
{
    /* Data path: writes WQEs to the send queue ring, then rings
     * the doorbell in the UAR (BF / BlueFlame register). */
    MLX5_VRB_LOG("post_send: stub (needs WQE ring + BF doorbell)");
    return -1;
}

int mlx5_ib_post_recv(struct ib_qp *qp, const void *wr, const void **bad_wr)
{
    MLX5_VRB_LOG("post_recv: stub (needs WQE ring)");
    return -1;
}

/* ============================================================
 * MR: Memory Registration
 * ============================================================ */

int mlx5_ib_dereg_mr(struct ib_mr *mr, void *udata)
{
    struct mlx5_ib_dev *dev = to_mdev(mr->device);
    uint8_t in_buf[16] = {0};
    uint8_t out_buf[16] = {0};

    /* DESTROY_MKEY input: mkey at offset 8 */
    *(uint32_t *)(in_buf) = OSSwapInt32(mr->lkey >> 8);  /* mkey index */

    uint8_t fw_status = 0xFF;
    IOReturn ret = mlx5_cmd_exec(dev->cmd_ctx,
                                 MLX5_CMD_OP_DESTROY_MKEY, 0,
                                 in_buf, 4,
                                 out_buf, sizeof(out_buf),
                                 &fw_status);
    return (ret == kIOReturnSuccess) ? 0 : -1;
}

struct ib_mr *mlx5_ib_reg_user_mr(struct ib_pd *pd, uint64_t start,
                                  uint64_t length, uint64_t virt_addr,
                                  int access_flags, void *udata)
{
    /* reg_user_mr maps userspace memory for RDMA access:
     * 1. Pin the user pages (get physical addresses)
     * 2. Send CREATE_MKEY command with the page list
     * 3. Firmware creates an MTT (Memory Translation Table) entry
     * 4. Returns lkey/rkey for referencing this memory
     *
     * Full implementation needs DMA page mapping + MTT setup.
     */
    MLX5_VRB_LOG("reg_user_mr: stub (needs DMA page mapping + MTT)");
    return NULL;
}

/* ============================================================
 * AH: Address Handle (RoCEv2 routing)
 * ============================================================ */

int mlx5_ib_create_ah(struct ib_ah *ah, void *attr, uint32_t flags,
                      void *udata)
{
    /* For RoCEv2, AH creation is done in software (no firmware command).
     * The AH contains the destination IP, MAC, and RoCEv2 GID.
     * It's resolved via ARP/GID cache before post_send. */
    MLX5_VRB_LOG("create_ah: software AH (no firmware command)");
    return 0;
}

int mlx5_ib_destroy_ah(struct ib_ah *ah, uint32_t flags)
{
    return 0;  /* Nothing to destroy for software AH */
}

/* ============================================================
 * Device queries
 * ============================================================ */

int mlx5_ib_query_device(struct ib_device *ibdev,
                         struct ib_device_attr *attr, void *uhw)
{
    struct mlx5_ib_dev *dev = to_mdev(ibdev);
    if (!attr) return -1;

    memset(attr, 0, sizeof(*attr));
    attr->vendor_id   = MLX5_VENDOR_ID;
    attr->vendor_part_id = MLX5_DEV_CX6_LX;
    attr->max_qp      = dev->max_qp;
    attr->max_cq      = dev->max_cq;
    attr->max_mr      = dev->max_mr;
    attr->max_pd      = dev->max_pd;
    attr->phys_port_cnt = dev->num_ports;
    attr->node_guid   = dev->node_guid;

    MLX5_VRB_LOG("query_device: max_qp=%d max_cq=%d", attr->max_qp, attr->max_cq);
    return 0;
}

int mlx5_ib_query_port(struct ib_device *ibdev, uint32_t port, void *props)
{
    /* QUERY_PORT is mostly done via firmware QUERY_HCA_CAP + registers.
     * For RoCE, port state comes from the netdev link state.
     * Returns port attributes: state, max_mtu, gid_table_len, etc. */
    MLX5_VRB_LOG("query_port(%d): stub", port);
    return 0;
}

/* ============================================================
 * mlx5_ib_init_ops — Fill the ib_device_ops struct
 *
 * Called during device registration to set all function pointers.
 * ============================================================ */
void mlx5_ib_init_ops(struct ib_device_ops *ops)
{
    memset(ops, 0, sizeof(*ops));

    /* Device operations */
    ops->query_device = mlx5_ib_query_device;
    ops->query_port   = mlx5_ib_query_port;

    /* PD operations */
    ops->alloc_pd     = mlx5_ib_alloc_pd;
    ops->dealloc_pd   = mlx5_ib_dealloc_pd;

    /* CQ operations */
    ops->create_cq    = mlx5_ib_create_cq;
    ops->destroy_cq   = mlx5_ib_destroy_cq;
    ops->poll_cq      = mlx5_ib_poll_cq;
    ops->req_notify_cq = mlx5_ib_req_notify_cq;

    /* QP operations */
    ops->create_qp    = mlx5_ib_create_qp;
    ops->modify_qp    = mlx5_ib_modify_qp;
    ops->destroy_qp   = mlx5_ib_destroy_qp;
    ops->post_send    = mlx5_ib_post_send;
    ops->post_recv    = mlx5_ib_post_recv;

    /* MR operations */
    ops->reg_user_mr  = mlx5_ib_reg_user_mr;
    ops->dereg_mr     = mlx5_ib_dereg_mr;

    /* AH operations */
    ops->create_ah    = mlx5_ib_create_ah;
    ops->destroy_ah   = mlx5_ib_destroy_ah;

    MLX5_VRB_LOG("ib_device_ops initialized (14 verbs registered)");
}
