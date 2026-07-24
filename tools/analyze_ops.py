#!/usr/bin/env python3
"""
Convert dump_ops hex output to raw binary files for objdump disassembly.
Then disassemble each function looking for BL (function call) instructions.

BL in ARM64: 100101 imm26 (opcode bits [31:26] = 0x25, i.e. 0b100101)
In practice: instruction & 0xFC000000 == 0x94000000
"""
import struct
import sys

# The dump_ops output (hex, 256 bytes per function)
alloc_pd_hex = (
    "7f2303d5ff4301d1f44f03a9fd7b04a9"
    "fd030191f40300aa8878059008e943f9"
    "080140f9a8831ef8ff1f00b9ffff00a9"
    "13b040f9e00313aa01008052b60b0094"
    "60010034ff430079e0fffff000000091"
    "030000f063ec0e91e4830091e10313aa"
    "02008052450080524f0b0094a23194d2"
    "e260b2f20208c0f24221e0f220008052"
    "01ca8052940b0094f30300aa000400b4"
    "e2230091e4730091e00314aae10313aa"
    "0302805285008052530b0094a0000034"
    "e00313aa4c0b0094130080d214000014"
    "94b240f9e00314aa01008052920b0094"
    "e0010034680a40b9090000f020a140fd"
    "e02300bde82700b9e0fffff000000091"
    "030000f063600f91e4830091e10314aa"
)

poll_cq_hex = (
    "7f2303d5f85fbca9f65701a9f44f02a9"
    "fd7b03a9fd7b03a9fdc3009114008052"
    "80050054f60301aa3f04007105050054"
    "f50302aae2040054f30300aa00600291"
    "be0d0094689e40b908020034170080d2"
    "14008052180680529f02166b6a030054"
    "685240f9007977f88156b89bff080094"
    "9402000bf7060091689e40b9ff0208eb"
    "6200005400000034e0230c91e82300bd"
    "00000034e0030c91e8070a91e0030caa"
    "02008052f50b009460000035e0030caa"
    "070b0094e80740f98978059029e943f9"
    "290140f93f0108ebc1000054e00314aa"
    "fd7b42a9f44f41a9ffc30091020c5fd6"
    "df0a0094e40300910202805245008052"
    "d0071eca5000f0b6208e38d4e00a0014"
)

post_send_hex = (
    "7f2303d5ff4301d1fc6f08a9fa6709a9"
    "f85f0aa9f6570ba9f44f0ca9fd7b0da9"
    "fd430391f50300aa080040f9080043f9"
    "e88b00a90800439116c10591133840b9"
    "082043911cc10591080042911a410591"
    "0800429108610591e81f00f909400591"
    "08600591e8a701a9e11700f9fb0301aa"
    "f33700b9681b40b91f050071eb0e0054"
)

def disasm_bl(func_name, hex_data, base_addr):
    raw = bytes.fromhex(hex_data)
    print(f"\n=== {func_name} @ 0x{base_addr:x} ===")

    for i in range(0, len(raw), 4):
        insn = struct.unpack_from("<I", raw, i)[0]
        pc = base_addr + i

        # BL (Branch with Link): 100101 imm26
        if (insn & 0xFC000000) == 0x94000000:
            imm26 = insn & 0x03FFFFFF
            # Sign-extend 26 bits
            if imm26 & 0x02000000:
                imm26 -= 0x04000000
            target = pc + (imm26 << 2)
            print(f"  0x{pc:x}: BL 0x{target:x}  (insn=0x{insn:08x})")

        # BLR (Branch with Link to Register): 1101011000111111000000rn000011
        elif (insn & 0xFFFFFC1F) == 0xD63F0000:
            rn = (insn >> 5) & 0x1F
            print(f"  0x{pc:x}: BLR x{rn}  (insn=0x{insn:08x})")

        # B (unconditional): 000101 imm26
        elif (insn & 0xFC000000) == 0x14000000:
            imm26 = insn & 0x03FFFFFF
            if imm26 & 0x02000000:
                imm26 -= 0x04000000
            target = pc + (imm26 << 2)
            print(f"  0x{pc:x}: B  0x{target:x}  (insn=0x{insn:08x})")

        # ADRP: 1_immlo_10000_immhi (loads page address)
        elif (insn & 0x9F000000) == 0x90000000:
            rd = insn & 0x1F
            immlo = (insn >> 29) & 0x3
            immhi = (insn >> 5) & 0x7FFFF
            imm = (immhi << 2) | immlo
            if imm & 0x100000:
                imm -= 0x200000
            page = (pc & ~0xFFF) + (imm << 12)
            print(f"  0x{pc:x}: ADRP x{rd}, 0x{page:x}")

        # MOV (immediate): for loading selector numbers
        elif (insn & 0xFF800000) == 0x52800000:  # MOVZ Wd, #imm
            rd = insn & 0x1F
            imm16 = (insn >> 5) & 0xFFFF
            if imm16 != 0:
                print(f"  0x{pc:x}: MOVZ w{rd}, #{imm16} (0x{imm16:x})")

        # SVC (system call): 11010100 0000 0 0 0 imm16 0 0 1
        elif (insn & 0xFFE0001F) == 0xD4000001:
            num = (insn >> 5) & 0xFFFF
            print(f"  0x{pc:x}: SVC #{num}  (syscall!)")

# Base addresses from dump_ops
disasm_bl("alloc_pd", alloc_pd_hex, 0x28af081c4)
disasm_bl("poll_cq", poll_cq_hex, 0x28af079b4)
disasm_bl("post_send", post_send_hex, 0x28af09b50)
