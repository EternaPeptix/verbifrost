/*
 * mlx5_registers.h — Key register offsets and structures for the
 * mlx5 HCA init segment and command interface.
 *
 * Based on the mlx5 firmware manual and Linux kernel's
 * drivers/net/ethernet/mellanox/mlx5/core/
 */

#ifndef MLX5_REGISTERS_H
#define MLX5_REGISTERS_H

#include <stdint.h>

/* BAR0 layout */
#define MLX5_INIT_SEG_OFFSET     0x1000   /* Init segment at +4KB in BAR0 */
#define MLX5_CMDQ_OFFSET         0x800    /* Command queue at +2KB in BAR0 */

/* Boot page size for firmware initialization */
#define MLX5_BOOT_PAGE_SIZE      (64 * 1024)  /* 64KB */

/* Init segment offsets within the init segment region */
#define MLX5_INIT_SEG_BOOT_ADDR_OFFSET   0x10    /* Boot page address */
#define MLX5_INIT_SEG_NIC_IF_OFFSET      0x40    /* NIC interface register */
#define MLX5_INIT_SEG_HEALTH_OFFSET      0x010   /* Health buffer offset */

/* ============================================================
 * mlx5_init_seg — The firmware initialization segment
 *
 * Located at BAR0 + 0x1000. Contains firmware version,
 * command queue parameters, and initialization state.
 * ============================================================ */
struct mlx5_init_seg
{
    uint32_t  fw_rev_major;           // offset 0x000
    uint32_t  fw_rev_minor;           // offset 0x004
    uint32_t  fw_rev_subminor;        // offset 0x008
    uint32_t  cmd_interface_rev;      // offset 0x00C

    uint32_t  reserved1[1];           // offset 0x010

    // Command queue parameters (offset 0x010 in real layout)
    uint32_t  log_cmd_strider;        // extracted from bits [16:8]
    uint32_t  log_cmd_max_lgth_sz;   // extracted from bits [28:24]

    uint32_t  reserved2[0x70];        // padding

    uint32_t  initializing;           // offset 0x1FC, bit 31
};

/* ============================================================
 * mlx5_cmd_context — Context for a single command submission
 * ============================================================ */
struct mlx5_cmd_context
{
    void    *cmd_buf;                 // Command mailbox (DMA)
    void    *resp_buf;                // Response mailbox (DMA)
    uint32_t cmd_handle;              // Command token/handle
    uint32_t status;                  // Completion status
};

/* ============================================================
 * mlx5 command opcodes (from firmware spec)
 * ============================================================ */
#define MLX5_CMD_OP_QUERY_HCA_CAP         0x100
#define MLX5_CMD_OP_SET_HCA_CAP            0x101
#define MLX5_CMD_OP_QUERY_ADAPTER          0x102
#define MLX5_CMD_OP_INIT_HCA               0x103
#define MLX5_CMD_OP_ENABLE_HCA             0x104
#define MLX5_CMD_OP_DISABLE_HCA            0x105
#define MLX5_CMD_OP_QUERY_PAGES            0x107
#define MLX5_CMD_OP_MANAGE_PAGES           0x108
#define MLX5_CMD_OP_TEARDOWN_HCA           0x10A
#define MLX5_CMD_OP_CREATE_EQ              0x301
#define MLX5_CMD_OP_DESTROY_EQ             0x302
#define MLX5_CMD_OP_CREATE_CQ              0x400
#define MLX5_CMD_OP_DESTROY_CQ             0x401
#define MLX5_CMD_OP_CREATE_QP              0x500
#define MLX5_CMD_OP_MODIFY_QP              0x501
#define MLX5_CMD_OP_DESTROY_QP             0x502
#define MLX5_CMD_OP_QUERY_QP               0x503
#define MLX5_CMD_OP_CREATE_MKEY            0x200
#define MLX5_CMD_OP_DESTROY_MKEY           0x201
#define MLX5_CMD_OP_QUERY_MKEY             0x202

/* Command status codes */
#define MLX5_CMD_STATUS_OK                 0x00
#define MLX5_CMD_STATUS_INT_ERR            0x01
#define MLX5_CMD_STATUS_BAD_OP             0x02
#define MLX5_CMD_STATUS_BAD_PARAM          0x03
#define MLX5_CMD_STATUS_BAD_SYS_STATE      0x04
#define MLX5_CMD_STATUS_BAD_RESOURCE       0x05
#define MLX5_CMD_STATUS_RESOURCE_BUSY      0x06

/* PCI vendor/device IDs */
#define MLX5_VENDOR_ID          0x15B3
#define MLX5_DEV_CX6_LX         0x1015   /* ConnectX-6 LX */
#define MLX5_DEV_CX6            0x101B   /* ConnectX-6 */
#define MLX5_DEV_CX7            0x1021   /* ConnectX-7 */

#endif /* MLX5_REGISTERS_H */
