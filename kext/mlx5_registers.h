#ifndef MLX5_REGISTERS_H
#define MLX5_REGISTERS_H

#include <stdint.h>

/* BAR0 layout */
#define MLX5_INIT_SEG_OFFSET     0x1000

/* Init segment register offsets (all big-endian) */
#define MLX5_ISEG_FW_REV_MAJOR       0x000
#define MLX5_ISEG_FW_REV_MINOR       0x004
#define MLX5_ISEG_FW_REV_SUBMINOR    0x008
#define MLX5_ISEG_CMDIF_REV          0x00C

/* Command queue params: bits [20:16]=log_size, [12:8]=log_stride */
#define MLX5_ISEG_CMDQ_PARAMS        0x010
#define MLX5_CMDQ_LOG_SIZE_SHIFT     16
#define MLX5_CMDQ_LOG_SIZE_MASK      0x1F
#define MLX5_CMDQ_LOG_STRIDE_SHIFT   8
#define MLX5_CMDQ_LOG_STRIDE_MASK    0x1F

#define MLX5_ISEG_CMDQ_ADDR_L        0x014
#define MLX5_ISEG_CMDQ_ADDR_H        0x018
#define MLX5_ISEG_CMD_DOORBELL       0x01C
#define MLX5_ISEG_BOOT_PAGE_ADDR     0x020
#define MLX5_ISEG_HEALTH_BUFFER      0x040
#define MLX5_ISEG_INITIALIZING       0x1FC
#define MLX5_ISEG_INITIALIZING_BIT   (1U << 31)

/* Owner/status */
#define MLX5_CMD_OWNER_SW            0x0
#define MLX5_CMD_OWNER_HW            0x1
#define MLX5_CMD_STATUS_SUCCESS      0x0

/* Command delivery status */
#define MLX5_CMD_DELIVERY_STAT_OK              0x00
#define MLX5_CMD_DELIVERY_STAT_FW_ERR          0x06

/* Command status (in response) */
#define MLX5_CMD_STAT_OK               0x00
#define MLX5_CMD_STAT_INT_ERR          0x01
#define MLX5_CMD_STAT_BAD_OP           0x02
#define MLX5_CMD_STAT_BAD_PARAM        0x03
#define MLX5_CMD_STAT_BAD_SYS_STATE    0x04
#define MLX5_CMD_STAT_RESOURCE_BUSY    0x06

/* Command entry size */
#define MLX5_CMD_ENTRY_SIZE   64
#define MLX5_CMD_MAILBOX_SIZE 4096

/* Mailbox offsets */
#define MLX5_MBOX_OPCODE_OFF     0x00
#define MLX5_MBOX_UID_OFF        0x02
#define MLX5_MBOX_OP_MOD_OFF     0x06
#define MLX5_MBOX_STATUS_OFF     0x00
#define MLX5_MBOX_SYNDROME_OFF   0x04

/* Opcodes */
#define MLX5_CMD_OP_QUERY_HCA_CAP   0x100
#define MLX5_CMD_OP_INIT_HCA        0x103
#define MLX5_CMD_OP_ENABLE_HCA      0x104
#define MLX5_CMD_OP_QUERY_PAGES     0x107
#define MLX5_CMD_OP_CREATE_CQ       0x400
#define MLX5_CMD_OP_CREATE_QP       0x500
#define MLX5_CMD_OP_CREATE_MKEY     0x200

/* UID values */
#define MLX5_CMD_UID_DRIVER    0x0

#define MLX5_BOOT_PAGE_SIZE    (64 * 1024)
#define MLX5_MAX_CMD_ENTRIES   32
#define MLX5_CMD_TIMEOUT_MS    60000

/* PCI IDs */
#define MLX5_VENDOR_ID   0x15B3
#define MLX5_DEV_CX6_LX  0x1015
#define MLX5_DEV_CX6     0x101B
#define MLX5_DEV_CX7     0x1021

#endif /* MLX5_REGISTERS_H */
#define MLX5_CMD_OP_DESTROY_MKEY  0x201
