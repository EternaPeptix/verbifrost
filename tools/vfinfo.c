/*
 * vfinfo.c — Verbifrost RDMA device info tool
 *
 * The first Verbifrost code. Lists all RDMA devices on macOS using the
 * standard ibv_* API from librdma.dylib. This proves the verbs stack works
 * end-to-end before we start building the ConnectX provider.
 *
 * Build:
 *   cc -o vfinfo vfinfo.c -lrdma
 *
 * Run:
 *   ./vfinfo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <infiniband/verbs.h>

static const char *port_state_str(enum ibv_port_state state) {
    switch (state) {
        case IBV_PORT_NOP:        return "NOP";
        case IBV_PORT_DOWN:       return "DOWN";
        case IBV_PORT_INIT:       return "INIT";
        case IBV_PORT_ARMED:      return "ARMED";
        case IBV_PORT_ACTIVE:     return "ACTIVE";
        case IBV_PORT_ACTIVE_DEFER: return "ACTIVE_DEFER";
        default: return "UNKNOWN";
    }
}

static const char *mtu_str(enum ibv_mtu mtu) {
    switch (mtu) {
        case IBV_MTU_256:  return "256";
        case IBV_MTU_512:  return "512";
        case IBV_MTU_1024: return "1024";
        case IBV_MTU_2048: return "2048";
        case IBV_MTU_4096: return "4096";
        default: return "?";
    }
}

int main(int argc, char **argv) {
    int num_devices = 0;

    printf("Verbifrost — RDMA device info\n");
    printf("=============================\n\n");

    /* Enumerate devices */
    struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list) {
        fprintf(stderr, "Error: ibv_get_device_list failed\n");
        return 1;
    }

    if (num_devices == 0) {
        printf("No RDMA devices found.\n");
        printf("\nTo see ConnectX devices, Verbifrost needs a provider dext.\n");
        ibv_free_device_list(dev_list);
        return 0;
    }

    printf("Found %d RDMA device(s):\n\n", num_devices);

    for (int i = 0; i < num_devices; i++) {
        const char *name = ibv_get_device_name(dev_list[i]);
        printf("--- Device %d: %s ---\n", i, name ? name : "(null)");

        /* Open the device */
        struct ibv_context *ctx = ibv_open_device(dev_list[i]);
        if (!ctx) {
            printf("  [could not open device]\n\n");
            continue;
        }

        /* Query device attributes */
        struct ibv_device_attr dev_attr;
        memset(&dev_attr, 0, sizeof(dev_attr));
        int ret = ibv_query_device(ctx, &dev_attr);
        if (ret == 0) {
            printf("  Vendor ID:     0x%04x\n", dev_attr.vendor_id);
            printf("  Device ID:     0x%04x\n", dev_attr.vendor_part_id);
            printf("  Max MR:        %d\n", dev_attr.max_mr);
            printf("  Max QP:        %d\n", dev_attr.max_qp);
            printf("  Max CQ:        %d\n", dev_attr.max_cq);
            printf("  Max PD:        %d\n", dev_attr.max_pd);
            printf("  Max CQE:       %d\n", dev_attr.max_cqe);
        } else {
            printf("  ibv_query_device failed: %d\n", ret);
        }

        /* Query port 1 */
        struct ibv_port_attr port_attr;
        memset(&port_attr, 0, sizeof(port_attr));
        ret = ibv_query_port(ctx, 1, &port_attr);
        if (ret == 0) {
            printf("  Port 1 state:  %s\n", port_state_str(port_attr.state));
            printf("  Port 1 LID:    0x%04x\n", port_attr.lid);
            printf("  Port 1 MTU:    %s\n", mtu_str(port_attr.active_mtu));
            printf("  Port 1 GIDs:   %d\n", port_attr.gid_tbl_len);
        } else {
            printf("  ibv_query_port(1) failed: %d\n", ret);
        }

        /* Query GID table */
        if (port_attr.gid_tbl_len > 0) {
            printf("  GID table:\n");
            for (int g = 0; g < port_attr.gid_tbl_len && g < 4; g++) {
                union ibv_gid gid;
                memset(&gid, 0, sizeof(gid));
                ret = ibv_query_gid(ctx, 1, g, &gid);
                if (ret == 0) {
                    printf("    GID[%d]: "
                           "%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
                           "%02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
                           g,
                           gid.raw[0], gid.raw[1], gid.raw[2], gid.raw[3],
                           gid.raw[4], gid.raw[5], gid.raw[6], gid.raw[7],
                           gid.raw[8], gid.raw[9], gid.raw[10], gid.raw[11],
                           gid.raw[12], gid.raw[13], gid.raw[14], gid.raw[15]);
                }
            }
        }

        /* Test PD + MR allocation (control path) */
        struct ibv_pd *pd = ibv_alloc_pd(ctx);
        if (pd) {
            printf("  PD alloc:      OK\n");

            /* Register a test memory region */
            char buf[4096] __attribute__((aligned(4096)));
            struct ibv_mr *mr = ibv_reg_mr(pd, buf, sizeof(buf),
                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                IBV_ACCESS_REMOTE_WRITE);
            if (mr) {
                printf("  MR register:   OK (lkey=0x%08x rkey=0x%08x)\n",
                       mr->lkey, mr->rkey);
                ibv_dereg_mr(mr);
            } else {
                printf("  MR register:   FAILED\n");
            }
            ibv_dealloc_pd(pd);
        } else {
            printf("  PD alloc:      FAILED\n");
        }

        /* Test CQ creation */
        struct ibv_cq *cq = ibv_create_cq(ctx, 64, NULL, NULL, 0);
        if (cq) {
            printf("  CQ create:     OK\n");
            ibv_destroy_cq(cq);
        } else {
            printf("  CQ create:     FAILED\n");
        }

        ibv_close_device(ctx);
        printf("\n");
    }

    ibv_free_device_list(dev_list);
    return 0;
}
