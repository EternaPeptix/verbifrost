/*
 * mach_interpose.c — Interpose mach_msg to trace libibverbs ↔ kernel IPC.
 *
 * Build: cc -dynamiclib -o mach_interpose.dylib mach_interpose.c
 * Run:   DYLD_INSERT_LIBRARIES=./mach_interpose.dylib ./vfinfo
 *
 * This intercepts mach_msg_overwrite and mach_msg2 to log the message
 * IDs and sizes that libibverbs sends to the kernel IOKit server.
 */
#include <stdio.h>
#include <string.h>
#include <mach/mach.h>
#include <mach/message.h>
#include <dlfcn.h>
#include <pthread.h>

static mach_msg_return_t (*real_mach_msg_overwrite)(
    mach_msg_header_t *msg,
    mach_msg_option_t option,
    mach_msg_size_t send_size,
    mach_msg_size_t rcv_size,
    mach_port_name_t rcv_name,
    mach_msg_timeout_t timeout,
    mach_port_name_t notify) = NULL;

static void init_real(void) {
    if (!real_mach_msg_overwrite) {
        real_mach_msg_overwrite = dlsym(RTLD_NEXT, "mach_msg_overwrite");
    }
}

/* Check if a message is going to an IOKit master port (high number) */
static int is_iokit_msg(mach_msg_header_t *msg) {
    /* IOKit service ports are typically large mach port names */
    if (msg->msgh_bits & MACH_MSGH_BITS_COMPLEX) {
        return 0; /* complex messages have port descriptors */
    }
    return msg->msgh_remote_port > 0x1000;
}

mach_msg_return_t mach_msg_overwrite(
    mach_msg_header_t *msg,
    mach_msg_option_t option,
    mach_msg_size_t send_size,
    mach_msg_size_t rcv_size,
    mach_port_name_t rcv_name,
    mach_msg_timeout_t timeout,
    mach_port_name_t notify) {

    init_real();

    /* Log IOKit-bound messages */
    if (is_iokit_msg(msg) && send_size > 0) {
        uint32_t msg_id = msg->msgh_id;
        fprintf(stderr,
            "[mach_msg] port=0x%x id=%u send=%u rcv=%u opt=0x%x\n",
            msg->msgh_remote_port, msg_id,
            send_size, rcv_size, option);

        /* Dump first 64 bytes of the message body */
        if (send_size > sizeof(mach_msg_header_t)) {
            uint8_t *body = (uint8_t *)(msg + 1);
            size_t body_len = send_size - sizeof(mach_msg_header_t);
            if (body_len > 64) body_len = 64;
            fprintf(stderr, "  body(%zu): ", body_len);
            for (size_t i = 0; i < body_len; i++) {
                fprintf(stderr, "%02x", body[i]);
            }
            fprintf(stderr, "\n");
        }
    }

    mach_msg_return_t ret = real_mach_msg_overwrite(
        msg, option, send_size, rcv_size, rcv_name, timeout, notify);

    /* Log the response */
    if (is_iokit_msg(msg) && (option & MACH_RCV_MSG) && rcv_size > 0) {
        fprintf(stderr, "[mach_msg] RESPONSE id=%u rcv_size=%u ret=0x%x\n",
            msg->msgh_id, rcv_size, ret);
    }

    return ret;
}
