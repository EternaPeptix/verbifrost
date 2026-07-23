/*
 * trace_verbs.c — Call ibv_* functions and trace what kernel calls
 * librdma.dylib makes. Uses DTrace-style approach: print every
 * IOKit function we can detect.
 *
 * Build: cc -o trace_verbs trace_verbs.c -lrdma -Wall
 * Run:   DYLD_PRINT_APIS=1 ./trace_verbs
 */
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <infiniband/verbs.h>

int main(void) {
    int num_devices = 0;

    printf("=== librdma.dylib verbs API trace ===\n\n");

    /* Find the library's real path */
    void *handle = dlopen("librdma.dylib", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        printf("Cannot load librdma.dylib: %s\n", dlerror());
        return 1;
    }

    /* Get the library info */
    Dl_info info;
    void *sym = dlsym(handle, "ibv_open_device");
    if (dladdr(sym, &info)) {
        printf("Library path: %s\n", info.dli_fname);
        printf("Base address: %p\n\n", info.dli_fbase);
    }

    /* Enumerate all exported symbols we can find */
    printf("=== Checking for non-ibv symbols ===\n");
    const char *extra_syms[] = {
        "ibv_register_driver",     /* Linux: provider registration */
        "ibv_driver_init",
        "verbs_register_provider",
        "ibv_get_device_list_ex",
        "ibv_open_device_ex",
        "ibv_query_device_ex",
        "rdma_get_devices",
        "rdma_create_id",
        "rdma_resolve_addr",       /* librdmacm */
        "rdma_resolve_route",
        "rdma_connect",
        "rdma_listen",
        "rdma_accept",
        NULL
    };

    for (int i = 0; extra_syms[i]; i++) {
        void *s = dlsym(handle, extra_syms[i]);
        if (s) {
            printf("  FOUND: %s at %p\n", extra_syms[i], s);
        }
    }

    /* List devices */
    printf("\n=== ibv_get_device_list ===\n");
    struct ibv_device **devs = ibv_get_device_list(&num_devices);
    printf("Found %d devices\n", num_devices);

    /* Open the active device and dump the context ops table */
    printf("\n=== ibv_open_device ===\n");
    for (int i = 0; i < num_devices; i++) {
        struct ibv_context *ctx = ibv_open_device(devs[i]);
        if (!ctx) continue;

        struct ibv_port_attr port;
        if (ibv_query_port(ctx, 1, &port) != 0) continue;

        const char *name = ibv_get_device_name(devs[i]);
        printf("\nDevice %s (state=%d):\n", name, port.state);

        /* The ibv_context contains the ops table — print cmd_fd */
        printf("  ctx->cmd_fd = %d\n", ctx->cmd_fd);
        printf("  ctx->async_fd = %d\n", ctx->async_fd);

        /* Check the ops function pointers */
        printf("  ops.poll_cq   = %p\n", (void *)ctx->ops.poll_cq);
        printf("  ops.post_send = %p\n", (void *)ctx->ops.post_send);
        printf("  ops.post_recv = %p\n", (void *)ctx->ops.post_recv);

        /* Try to get more info from the context */
        if (port.state == IBV_PORT_ACTIVE) {
            printf("\n  ACTIVE device — trying verbs operations:\n");

            struct ibv_pd *pd = ibv_alloc_pd(ctx);
            printf("  ibv_alloc_pd: %s (%p)\n", pd ? "OK" : "FAILED", pd);

            if (pd) {
                struct ibv_cq *cq = ibv_create_cq(ctx, 64, NULL, NULL, 0);
                printf("  ibv_create_cq: %s (%p)\n", cq ? "OK" : "FAILED", cq);

                if (cq) {
                    /* Try to create a QP */
                    struct ibv_qp_init_attr attr = {};
                    attr.send_cq = cq;
                    attr.recv_cq = cq;
                    attr.cap.max_send_wr = 4;
                    attr.cap.max_recv_wr = 4;
                    attr.cap.max_send_sge = 1;
                    attr.cap.max_recv_sge = 1;
                    attr.cap.max_inline_data = 64;
                    attr.qp_type = IBV_QPT_RC;

                    struct ibv_qp *qp = ibv_create_qp(pd, &attr);
                    printf("  ibv_create_qp: %s (%p)\n", qp ? "OK" : "FAILED", qp);
                    if (qp) {
                        printf("    qp_num=%u\n", qp->qp_num);
                        ibv_destroy_qp(qp);
                    }
                    ibv_destroy_cq(cq);
                }
                ibv_dealloc_pd(pd);
            }
        }

        ibv_close_device(ctx);
    }

    ibv_free_device_list(devs);

    /* Check for librdmacm (RDMA Connection Manager) */
    printf("\n=== Checking for librdmacm ===\n");
    void *cm = dlopen("librdmacm.dylib", RTLD_NOW);
    if (cm) {
        printf("librdmacm.dylib loaded!\n");
        void *s = dlsym(cm, "rdma_get_devices");
        if (s) printf("  rdma_get_devices found\n");
    } else {
        printf("librdmacm.dylib not available\n");
    }

    /* Check for provider plugins */
    printf("\n=== Checking for provider plugins ===\n");
    const char *plugin_paths[] = {
        "/usr/lib/rdma/",
        "/usr/local/lib/rdma/",
        "/opt/rdma/",
        NULL
    };
    for (int i = 0; plugin_paths[i]; i++) {
        printf("  %s: checking...\n", plugin_paths[i]);
        /* (would use opendir here but keeping it simple) */
    }

    printf("\n=== Done ===\n");
    return 0;
}


