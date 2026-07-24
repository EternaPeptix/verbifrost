/*
 * resolve_got.c — Resolve the GOT entries that alloc_pd's PLT stubs
 * point to. This tells us exactly which external functions are called.
 *
 * Build: cc -o resolve_got resolve_got.c -lrdma -Wall
 */
#include <stdio.h>
#include <dlfcn.h>
#include <infiniband/verbs.h>

/* GOT entry addresses from decode_plt.py */
static const struct {
    const char *stub_name;
    unsigned long got_addr;
} entries[] = {
    {"target_0b0d8 (first call in alloc_pd)", 0x2a09b6730},
    {"target_0b098 (w0=1, w1=0x650)",         0x2a09b5fb8},
    {"target_0af68 (w5=2)",                   0x2a09b6680},
    {"target_0afb8 (w3=16, w5=4)",            0x2a09aa788},
    {"target_0afa8",                           0x2a09b6718},
    {NULL, 0}
};

int main(void) {
    /* We need libibverbs loaded to access the GOT */
    void *lib = dlopen("librdma.dylib", RTLD_NOW | RTLD_GLOBAL);
    if (!lib) {
        printf("Cannot load librdma: %s\n", dlerror());
        return 1;
    }

    /* Open a device to force the GOT to be resolved */
    int n = 0;
    struct ibv_device **devs = ibv_get_device_list(&n);
    if (n > 0) {
        ibv_open_device(devs[0]);
    }

    printf("=== GOT Entry Resolution ===\n\n");

    for (int i = 0; entries[i].stub_name; i++) {
        unsigned long *got_ptr = (unsigned long *)entries[i].got_addr;
        unsigned long func_addr = *got_ptr;

        Dl_info info;
        if (dladdr((void *)func_addr, &info) && info.dli_sname) {
            printf("  %s\n", entries[i].stub_name);
            printf("    GOT[0x%lx] = 0x%lx\n", entries[i].got_addr, func_addr);
            printf("    → %s (%s)\n\n", info.dli_sname, info.dli_fname);
        } else {
            printf("  %s\n", entries[i].stub_name);
            printf("    GOT[0x%lx] = 0x%lx\n", entries[i].got_addr, func_addr);
            printf("    → (unresolved)\n\n");
        }
    }

    ibv_free_device_list(devs);
    return 0;
}
