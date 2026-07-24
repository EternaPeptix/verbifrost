/*
 * resolve_dispatch.c — Resolve the core dispatch function called by
 * ibv_cmd_alloc_pd. This is the function that does the actual kernel IPC.
 *
 * Build: cc -o resolve_dispatch resolve_dispatch.c -lrdma -Wall
 */
#include <stdio.h>
#include <dlfcn.h>
#include <infiniband/verbs.h>

int main(void) {
    void *lib = dlopen("librdma.dylib", RTLD_NOW | RTLD_GLOBAL);
    int n = 0;
    struct ibv_device **devs = ibv_get_device_list(&n);
    if (n > 0) ibv_open_device(devs[0]);

    /* BL targets from ibv_cmd_alloc_pd disassembly */
    unsigned long addrs[] = {
        0x28aeb3b88,   /* main dispatch call from ibv_cmd_alloc_pd */
        0x28aeb3b48,   /* second code path */
        0x28aeb3b68,   /* third code path */
        0
    };

    printf("=== Resolving ibv_cmd_alloc_pd dispatch targets ===\n\n");
    for (int i = 0; addrs[i]; i++) {
        Dl_info info;
        void *p = (void *)addrs[i];
        if (dladdr(p, &info) && info.dli_sname) {
            printf("  0x%lx → %s (%s)\n", addrs[i],
                   info.dli_sname, info.dli_fname);
        } else {
            printf("  0x%lx → (unknown, base=%s)\n", addrs[i],
                   info.dli_fname ? info.dli_fname : "?");
        }
    }

    /* Also check what symbols are in the 0x28aeb3xxx range */
    printf("\n=== Checking nearby cmd functions ===\n");
    const char *syms[] = {
        "ibv_cmd_alloc_pd",
        "ibv_cmd_dealloc_pd",
        "ibv_cmd_reg_mr",
        "ibv_cmd_dereg_mr",
        "ibv_cmd_create_cq",
        "ibv_cmd_destroy_cq",
        "ibv_cmd_create_qp",
        "ibv_cmd_modify_qp",
        "ibv_cmd_destroy_qp",
        "ibv_cmd_query_device",
        "ibv_cmd_query_port",
        "ibv_cmd_post_send",
        "ibv_cmd_post_recv",
        "ibv_cmd_poll_cq",
        "ibv__init",
        "_init",
        NULL
    };

    for (int i = 0; syms[i]; i++) {
        void *s = dlsym(lib, syms[i]);
        if (s) {
            Dl_info info;
            dladdr(s, &info);
            printf("  %s @ %p", syms[i], s);
            if (info.dli_sname) printf(" (%s)", info.dli_sname);
            printf("\n");
        }
    }

    /* Try to find the dispatch function by name */
    printf("\n=== Looking for internal dispatch functions ===\n");
    const char *dispatch_names[] = {
        "ibv_cmd",
        "_ibv_write",
        "write_to_fd",
        "__ibv_write_to_fd",
        "ibv_kern_write",
        "ibv_write_to_dev",
        "mach_iokit_msg",
        "iokit_write",
        "rdma_write",
        "__rdma_send_cmd",
        NULL
    };
    for (int i = 0; dispatch_names[i]; i++) {
        void *s = dlsym(lib, dispatch_names[i]);
        if (s) printf("  FOUND: %s @ %p\n", dispatch_names[i], s);
    }

    ibv_free_device_list(devs);
    return 0;
}
