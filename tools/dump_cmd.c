/*
 * dump_cmd.c — Dump ibv_cmd_alloc_pd and ibv_cmd_create_cq to find
 * the actual kernel IPC mechanism (mach_msg vs write vs IOConnect).
 *
 * ibv_cmd_* functions are where the real kernel communication happens.
 * On Linux these call write(fd, ...). On macOS they must use mach_msg.
 *
 * Build: cc -o dump_cmd dump_cmd.c -lrdma -Wall
 */
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <infiniband/verbs.h>

int main(void) {
    void *lib = dlopen("librdma.dylib", RTLD_NOW | RTLD_GLOBAL);

    /* Get the addresses of the cmd functions */
    void *cmd_funcs[] = {
        dlsym(lib, "ibv_cmd_alloc_pd"),
        dlsym(lib, "ibv_cmd_create_cq"),
        dlsym(lib, "ibv_cmd_create_qp"),
        dlsym(lib, "ibv_cmd_reg_mr"),
        NULL
    };
    const char *names[] = {
        "ibv_cmd_alloc_pd",
        "ibv_cmd_create_cq",
        "ibv_cmd_create_qp",
        "ibv_cmd_reg_mr",
        NULL
    };

    for (int i = 0; cmd_funcs[i]; i++) {
        Dl_info info;
        dladdr(cmd_funcs[i], &info);
        printf("# %s @ %p\n", names[i], cmd_funcs[i]);

        unsigned char *code = (unsigned char *)cmd_funcs[i];
        for (int j = 0; j < 512; j++) {
            printf("%02x", code[j]);
            if ((j + 1) % 16 == 0) printf("\n");
        }
        printf("\n\n");
    }

    return 0;
}
