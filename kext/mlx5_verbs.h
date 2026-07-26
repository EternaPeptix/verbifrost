#ifndef MLX5_VERBS_H
#define MLX5_VERBS_H

/*
 * mlx5_verbs.h — mlx5 RDMA verbs implementations (ib_device_ops).
 *
 * These functions fill ib_device_ops with mlx5-specific implementations.
 * They translate standard RDMA verbs (alloc_pd, create_cq, create_qp,
 * reg_user_mr, post_send, etc.) into mlx5 firmware commands.
 *
 * Ported from Linux drivers/infiniband/hw/mlx5/main.c, qp.c, cq.c, mr.c
 */

#include "mlx5_cmd.h"
#include "rdma/ib_verbs.h"

/* ============================================================
 * mlx5_ib_dev — Provider private data (stored in ib_device.provider_priv)
 * ============================================================ */
struct mlx5_ib_dev {
    struct ib_device  ib_dev;
    struct mlx5_cmd_context *cmd_ctx;

    /* Cached capabilities */
    uint32_t  max_qp;
    uint32_t  max_cq;
    uint32_t  max_mr;
    uint32_t  max_pd;
    uint32_t  num_ports;
    __be64    node_guid;
};

/* Helper: get mlx5_ib_dev from ib_device */
static inline struct mlx5_ib_dev *to_mdev(struct ib_device *ibdev)
{
    return (struct mlx5_ib_dev *)ibdev->provider_priv;
}

/* ============================================================
 * ib_device_ops implementation (verbs layer)
 * ============================================================ */

/* PD: Protection Domain */
int mlx5_ib_alloc_pd(struct ib_pd *pd, void *udata);
int mlx5_ib_dealloc_pd(struct ib_pd *pd, void *udata);

/* CQ: Completion Queue */
int mlx5_ib_create_cq(struct ib_cq *cq, void *cq_attr, void *udata);
int mlx5_ib_destroy_cq(struct ib_cq *cq, void *udata);
int mlx5_ib_poll_cq(struct ib_cq *cq, int ne, struct ib_wc *wc);
int mlx5_ib_req_notify_cq(struct ib_cq *cq, int flags);

/* QP: Queue Pair */
int mlx5_ib_create_qp(struct ib_qp *qp, struct ib_qp_init_attr *init,
                      void *udata);
int mlx5_ib_modify_qp(struct ib_qp *qp, int mask, void *attr, void *udata);
int mlx5_ib_destroy_qp(struct ib_qp *qp, void *udata);
int mlx5_ib_post_send(struct ib_qp *qp, const void *wr, const void **bad_wr);
int mlx5_ib_post_recv(struct ib_qp *qp, const void *wr, const void **bad_wr);

/* MR: Memory Registration */
struct ib_mr *mlx5_ib_reg_user_mr(struct ib_pd *pd, uint64_t start,
                                  uint64_t length, uint64_t virt_addr,
                                  int access_flags, void *udata);
int mlx5_ib_dereg_mr(struct ib_mr *mr, void *udata);

/* AH: Address Handle (RoCEv2 routing) */
int mlx5_ib_create_ah(struct ib_ah *ah, void *attr, uint32_t flags,
                      void *udata);
int mlx5_ib_destroy_ah(struct ib_ah *ah, uint32_t flags);

/* Device queries */
int mlx5_ib_query_device(struct ib_device *dev, struct ib_device_attr *attr,
                         void *uhw);
int mlx5_ib_query_port(struct ib_device *dev, uint32_t port, void *props);

/* Initialize the ib_device_ops struct for mlx5 */
void mlx5_ib_init_ops(struct ib_device_ops *ops);

#endif /* MLX5_VERBS_H */
