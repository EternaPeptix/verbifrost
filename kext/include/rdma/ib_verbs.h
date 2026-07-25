#ifndef IORDMA_IB_VERBS_H
#define IORDMA_IB_VERBS_H

/*
 * ib_verbs.h — Reconstructed kernel-side RDMA structs for linking
 * against IORDMAFamily.kext.
 *
 * IORDMAFamily is a port of Linux drivers/infiniband/core/. The
 * exported symbols (ib_register_device, ib_alloc_device, etc.) use
 * the same struct layouts as the Linux kernel.
 *
 * This header provides the minimal struct definitions needed to:
 *   1. Allocate an ib_device via ib_alloc_device()
 *   2. Fill ib_device_ops with provider implementations
 *   3. Register via ib_register_device()
 *
 * The struct layouts match Linux's include/rdma/ib_verbs.h.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct ib_device;
struct ib_pd;
struct ib_cq;
struct ib_qp;
struct ib_mr;
struct ib_ah;
struct ib_ucontext;

/* ============================================================
 * Core types
 * ============================================================ */

typedef uint64_t __be64;
typedef uint32_t __be32;
typedef uint16_t __be16;

struct ib_device_attr {
    uint64_t  fw_ver;
    __be64    node_guid;
    __be64    sys_image_guid;
    uint64_t  max_mr_size;
    uint64_t  page_size_cap;
    uint32_t  vendor_id;
    uint32_t  vendor_part_id;
    uint32_t  hw_rev;
    uint32_t  max_qp;
    uint32_t  max_qp_wr;
    uint32_t  max_sge;
    uint32_t  max_cq;
    uint32_t  max_cqe;
    uint32_t  max_mr;
    uint32_t  max_pd;
    uint32_t  max_qp_init_rd_atom;
    uint32_t  max_qp_rd_atom;
    uint32_t  max_srq;
    uint32_t  max_srq_wr;
    uint32_t  max_srq_sge;
    uint16_t  max_pkeys;
    uint8_t   local_ca_ack_delay;
    uint8_t   phys_port_cnt;
};

enum ib_port_state {
    IB_PORT_NOP        = 0,
    IB_PORT_DOWN       = 1,
    IB_PORT_INIT       = 2,
    IB_PORT_ARMED      = 3,
    IB_PORT_ACTIVE     = 4,
    IB_PORT_ACTIVE_DEFER = 5,
};

enum ib_qp_state {
    IB_QPS_RESET,
    IB_QPS_INIT,
    IB_QPS_RTR,
    IB_QPS_RTS,
    IB_QPS_SQD,
    IB_QPS_SQE,
    IB_QPS_ERR,
    IB_QPS_UNKNOWN,
};

enum ib_qp_type {
    IB_QPT_SMI,
    IB_QPT_GSI,
    IB_QPT_RC,
    IB_QPT_UC,
    IB_QPT_UD,
    IB_QPT_RAW_PACKET,
    IB_QPT_XRC_INI,
    IB_QPT_XRC_TGT,
    IB_QPT_MAX,
};

enum ib_wr_opcode {
    IB_WR_RDMA_WRITE,
    IB_WR_RDMA_WRITE_WITH_IMM,
    IB_WR_SEND,
    IB_WR_SEND_WITH_IMM,
    IB_WR_RDMA_READ,
    IB_WR_ATOMIC_CMP_AND_SWP,
    IB_WR_ATOMIC_FETCH_AND_ADD,
    IB_WR_LSO,
    IB_WR_SEND_WITH_INV,
    IB_WR_RDMA_READ_WITH_INV,
    IB_WR_LOCAL_INV,
    IB_WR_REG_MR,
    IB_WR_REG_CQ, /* macOS extension? */
    IB_WR_REG_MR_EXP,
    IB_WR_LOCAL_INV_NO_EXEC,
};

enum ib_wc_status {
    IB_WC_SUCCESS,
    IB_WC_LOC_LEN_ERR,
    IB_WC_LOC_QP_OP_ERR,
    IB_WC_LOC_EEC_OP_ERR,
    IB_WC_LOC_PROT_ERR,
    IB_WC_WR_FLUSH_ERR,
    IB_WC_MW_BIND_ERR,
    IB_WC_BAD_RESP_ERR,
    IB_WC_LOC_ACCESS_ERR,
    IB_WC_REM_INV_REQ_ERR,
    IB_WC_REM_ACCESS_ERR,
    IB_WC_REM_OP_ERR,
    IB_WC_RETRY_EXC_ERR,
    IB_WC_RNR_RETRY_EXC_ERR,
    IB_WC_LOC_RDD_VIOL_ERR,
    IB_WC_REM_RDD_VIOL_ERR,
    IB_WC_REM_ABORT_ERR,
    IB_WC_INV_EECN_ERR,
    IB_WC_INV_EEC_STATE_ERR,
    IB_WC_FATAL_ERR,
    IB_WC_RESP_TIMEOUT_ERR,
    IB_WC_GENERAL_ERR,
};

enum ib_access_flags {
    IB_ACCESS_LOCAL_WRITE     = 1,
    IB_ACCESS_REMOTE_WRITE    = (1<<1),
    IB_ACCESS_REMOTE_READ     = (1<<2),
    IB_ACCESS_REMOTE_ATOMIC   = (1<<3),
    IB_ACCESS_MW_BIND         = (1<<4),
    IB_ZERO_BASED             = (1<<5),
    IB_ACCESS_ON_DEMAND       = (1<<6),
    IB_ACCESS_HUGETLB         = (1<<7),
    IB_ACCESS_RELAXED_ORDERING = (1<<8),
};

/* ============================================================
 * ib_pd — Protection Domain
 * ============================================================ */
struct ib_pd {
    struct ib_device *device;
    struct ib_ucontext *uobject;
    uint32_t local_dma_lkey;
    uint32_t flags;
    uint32_t res; /* resource tracking */
};

/* ============================================================
 * ib_cq — Completion Queue
 * ============================================================ */
struct ib_cq {
    struct ib_device *device;
    struct ib_ucontext *uobject;
    void (*comp_handler)(struct ib_cq *, void *);
    void (*event_handler)(struct ib_event *, void *);
    void *cq_context;
    int cqe;
    /* ... kernel internals */
};

struct ib_wc {
    union {
        uint64_t wr_id;
        struct ib_cq *cq;
    } wr;
    enum ib_wc_status status;
    enum ib_wr_opcode opcode;
    uint32_t vendor_err;
    uint32_t byte_len;
    uint32_t imm_data;
    uint32_t qp_num;
    uint32_t src_qp;
    int wc_flags;
    uint16_t pkey_index;
    uint16_t slid;
    uint8_t sl;
    uint8_t dlid_path_bits;
};

/* ============================================================
 * ib_mr — Memory Region
 * ============================================================ */
struct ib_mr {
    struct ib_device *device;
    struct ib_pd *pd;
    struct ib_ucontext *uobject;
    uint32_t lkey;
    uint32_t rkey;
    uint64_t iova;
    uint64_t length;
};

/* ============================================================
 * ib_qp — Queue Pair
 * ============================================================ */
struct ib_qp {
    struct ib_device *device;
    struct ib_pd *pd;
    struct ib_cq *send_cq;
    struct ib_cq *recv_cq;
    struct ib_ucontext *uobject;
    void *qp_context;
    uint32_t qp_num;
    uint32_t max_inline_data;
    enum ib_qp_state state;
    enum ib_qp_type qp_type;
};

struct ib_qp_init_attr {
    void *qp_context;
    struct ib_cq *send_cq;
    struct ib_cq *recv_cq;
    struct ib_srq *srq;
    struct ib_xrcd *xrcd;
    struct ib_cq *ib_cq_ex;
    enum ib_qp_type qp_type;
    uint32_t sq_sig_type; /* 0=all, 1=by WR */
    uint32_t cap; /* struct ib_qp_cap { max_send_wr, max_recv_wr, ... } */
};

/* ============================================================
 * ib_ah — Address Handle (for RoCEv2 routing)
 * ============================================================ */
struct ib_ah {
    struct ib_device *device;
    struct ib_pd *pd;
    struct ib_ucontext *uobject;
};

struct ib_ah_attr {
    struct ib_global_route grh;
    uint16_t dlid;
    uint8_t port_num;
    uint8_t static_rate;
    uint8_t ah_flags;
    uint8_t sl;
    uint8_t src_path_bits;
};

/* ============================================================
 * ib_device_ops — THE PROVIDER INTERFACE
 *
 * This struct contains function pointers for every RDMA operation.
 * A provider (our mlx5 port) fills this in and calls ib_register_device.
 * ============================================================ */
struct ib_device_ops {
    /* Device operations */
    int (*query_device)(struct ib_device *dev, struct ib_device_attr *attr,
                        void *uhw);
    int (*query_port)(struct ib_device *dev, uint32_t port, void *props);
    int (*get_port_immutable)(struct ib_device *dev, uint32_t port);
    enum rdma_link_layer (*get_link_layer)(struct ib_device *dev,
                                           uint32_t port_num);
    int (*modify_device)(struct ib_device *dev, int mask, void *props);
    int (*modify_port)(struct ib_device *dev, uint32_t port, int mask,
                       void *props);

    /* PD operations */
    int (*alloc_pd)(struct ib_pd *pd, void *udata);
    int (*dealloc_pd)(struct ib_pd *pd, void *udata);

    /* CQ operations */
    int (*create_cq)(struct ib_cq *cq, void *cq_attr, void *udata);
    int (*destroy_cq)(struct ib_cq *cq, void *udata);
    int (*poll_cq)(struct ib_cq *cq, int ne, struct ib_wc *wc);
    int (*req_notify_cq)(struct ib_cq *cq, int flags);
    int (*resize_cq)(struct ib_cq *cq, int cqe, void *udata);

    /* QP operations */
    int (*create_qp)(struct ib_qp *qp, struct ib_qp_init_attr *init,
                     void *udata);
    int (*modify_qp)(struct ib_qp *qp, int mask, void *attr, void *udata);
    int (*destroy_qp)(struct ib_qp *qp, void *udata);
    int (*query_qp)(struct ib_qp *qp, void *attr, int mask, void *init_attr);
    int (*post_send)(struct ib_qp *qp, const void *wr, const void **bad_wr);
    int (*post_recv)(struct ib_qp *qp, const void *wr, const void **bad_wr);

    /* MR operations */
    struct ib_mr *(*reg_user_mr)(struct ib_pd *pd, uint64_t start,
                                 uint64_t length, uint64_t virt_addr,
                                 int access_flags, void *udata);
    int (*dereg_mr)(struct ib_mr *mr, void *udata);
    int (*map_mr_sg)(struct ib_mr *mr, void *sg, int sg_nents, void *sg_offset);

    /* AH operations */
    int (*create_ah)(struct ib_ah *ah, void *attr, uint32_t flags,
                     void *udata);
    int (*destroy_ah)(struct ib_ah *ah, uint32_t flags);

    /* Ucontext (per-process) */
    int (*alloc_ucontext)(struct ib_ucontext *uctx, void *udata);
    void (*dealloc_ucontext)(struct ib_ucontext *uctx);
    int (*mmap)(struct ib_ucontext *uctx, unsigned long offset,
                unsigned long length, void **vma);

    /* 100+ more operations for SRQ, MW, flow steering, etc. */
    void *reserved[64];
};

/* ============================================================
 * ib_device — The core device struct
 *
 * Allocated by ib_alloc_device(sizeof(provider_private_data)).
 * The provider fills .ops and .name, then calls ib_register_device.
 * ============================================================ */
struct ib_device {
    char name[64];           /* e.g., "mlx5_0" */
    struct ib_device_ops ops; /* ← THE PROVIDER INTERFACE */
    __be64 node_guid;
    __be64 node_desc[8];
    uint32_t local_dma_lkey;
    uint32_t flags;
    int num_comp_vectors;
    uint32_t phys_port_cnt;
    int (*get_port_immutable_fn)(struct ib_device *, uint32_t);
    void *core_data[16]; /* IORDMAFamily internal data */
    void *provider_priv; /* mlx5_ib_dev goes here */
};

/* ============================================================
 * Exported API (linked from com.apple.iokit.IORDMAFamily)
 * ============================================================ */

/* Allocate an ib_device with room for provider private data */
extern struct ib_device *ib_alloc_device(size_t priv_size);

/* Free a device */
extern void ib_dealloc_device(struct ib_device *device);

/* Register the device with the RDMA core (makes it visible to libibverbs) */
extern int ib_register_device(struct ib_device *device, const char *name,
                              void *dma_device);

/* Unregister */
extern void ib_unregister_device(struct ib_device *device);

/* Register/unregister a client (for event notifications) */
extern int ib_register_client(void *client);
extern void ib_unregister_client(void *client);

#ifdef __cplusplus
}
#endif

#endif /* IORDMA_IB_VERBS_H */
