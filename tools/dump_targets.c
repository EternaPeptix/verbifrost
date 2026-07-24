/*
 * dump_targets.c — Dump the code at the BL target addresses found in
 * alloc_pd/poll_cq to identify if they are mach_msg PLT stubs.
 *
 * Build: cc -o dump_targets dump_targets.c -lrdma -Wall
 */
#include <stdio.h>
#include <string.h>
#include <infiniband/verbs.h>

/* Target addresses from analyze_ops.py */
static const struct {
    const char *name;
    unsigned long addr;
} targets[] = {
    {"target_0b0d8", 0x28af0b0d8},
    {"target_0af68", 0x28af0af68},
    {"target_0b098", 0x28af0b098},  /* alloc_pd calls with w1=0x650 */
    {"target_0afb8", 0x28af0afb8},
    {"target_0afa8", 0x28af0afa8},
    {"target_0b0ec", 0x28af0b0ec},  /* poll_cq first call */
    {"target_09e1c", 0x28af09e1c},
    {"target_0aa2c", 0x28af0aa2c},
    {"target_0a680", 0x28af0a680},
    {"target_0a610", 0x28af0a610},
    {NULL, 0}
};

int main(void) {
    for (int i = 0; targets[i].name; i++) {
        unsigned char *code = (unsigned char *)targets[i].addr;
        printf("# %s @ 0x%lx\n", targets[i].name, targets[i].addr);
        for (int j = 0; j < 48; j++) {  /* 12 instructions = typical PLT stub */
            printf("%02x", code[j]);
            if ((j + 1) % 16 == 0) printf("\n");
        }
        printf("\n\n");
    }
    return 0;
}
