/*
 * dump_ops.c — Dump the machine code of libibverbs ops functions
 * so we can disassemble them with otool/objdump.
 *
 * Build: cc -o dump_ops dump_ops.c -lrdma -Wall
 * Run:   ./dump_ops
 */
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <infiniband/verbs.h>

int main(void) {
    int n = 0;
    struct ibv_device **devs = ibv_get_device_list(&n);

    for (int i = 0; i < n; i++) {
        struct ibv_context *ctx = ibv_open_device(devs[i]);
        if (!ctx) continue;
        struct ibv_port_attr port;
        ibv_query_port(ctx, 1, &port);
        if (port.state != IBV_PORT_ACTIVE) {
            ibv_close_device(ctx);
            continue;
        }

        printf("# Device: %s\n", ibv_get_device_name(devs[i]));
        printf("# ops table at %p, cmd_fd=%d\n\n", &ctx->ops, ctx->cmd_fd);

        /* Dump each ops function */
        struct {
            const char *name;
            void *ptr;
        } ops[] = {
            {"alloc_pd",       (void *)ctx->ops._compat_alloc_pd},
            {"dealloc_pd",     (void *)ctx->ops._compat_dealloc_pd},
            {"reg_mr",         (void *)ctx->ops._compat_reg_mr},
            {"dereg_mr",       (void *)ctx->ops._compat_dereg_mr},
            {"create_cq",      (void *)ctx->ops._compat_create_cq},
            {"poll_cq",        (void *)ctx->ops.poll_cq},
            {"req_notify_cq",  (void *)ctx->ops.req_notify_cq},
            {"destroy_cq",     (void *)ctx->ops._compat_destroy_cq},
            {"create_qp",      (void *)ctx->ops._compat_create_qp},
            {"modify_qp",      (void *)ctx->ops._compat_modify_qp},
            {"destroy_qp",     (void *)ctx->ops._compat_destroy_qp},
            {"post_send",      (void *)ctx->ops.post_send},
            {"post_recv",      (void *)ctx->ops.post_recv},
            {"query_device",   (void *)ctx->ops._compat_query_device},
            {"query_port",     (void *)ctx->ops._compat_query_port},
            {NULL, NULL}
        };

        for (int j = 0; ops[j].name; j++) {
            if (!ops[j].ptr) continue;
            printf("# %s @ %p\n", ops[j].name, ops[j].ptr);

            /* Write 256 bytes of the function to stdout as hex */
            unsigned char *code = (unsigned char *)ops[j].ptr;
            for (int k = 0; k < 256; k++) {
                printf("%02x", code[k]);
                if ((k + 1) % 16 == 0) printf("\n");
            }
            printf("\n\n");
        }

        ibv_close_device(ctx);
        break; /* only need one active device */
    }

    ibv_free_device_list(devs);
    return 0;
}
