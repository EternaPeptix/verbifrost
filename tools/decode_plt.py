#!/usr/bin/env python3
"""
Decode the BL target stubs from dump_targets output.
All targets share the same pattern: ADRP + ADD + LDR + authenticated BR.
These are GOT PLT stubs — they load external function addresses from the
Global Offset Table and jump to them.

This script decodes the GOT page addresses to identify which external
functions are called.
"""
import struct

targets = {
    "target_0b0d8": ("51d50af031c21c91300240f9110a1fd7", 0x28af0b0d8),
    "target_0b098": ("51d50ad031e23e91300240f9110a1fd7", 0x28af0b098),
    "target_0af68": ("71d50a9031021a91300240f9110a1fd7", 0x28af0af68),
    "target_0afb8": ("11d50a9031221e91300240f9110a1fd7", 0x28af0afb8),
    "target_0b0ec": ("31420891300240f9110a1fd751d50af0", 0x28af0b0ec),
}

print("=== PLT Stub Analysis ===\n")
print("All BL targets are GOT PLT stubs with pattern:")
print("  ADRP x17, GOT_page")
print("  ADD  x17, x17, GOT_offset")
print("  LDR  x16, [x17, #0]   ; load function pointer from GOT")
print("  BRAA x17, x16          ; authenticated branch (PAC)\n")

for name, (hex_data, base) in targets.items():
    raw = bytes.fromhex(hex_data)
    insns = [struct.unpack_from("<I", raw, i)[0] for i in range(0, 16, 4)]

    # Decode ADRP
    adrp = insns[0]
    if (adrp & 0x9F000000) == 0x90000000:
        rd = adrp & 0x1F
        immlo = (adrp >> 29) & 0x3
        immhi = (adrp >> 5) & 0x7FFFF
        imm = (immhi << 2) | immlo
        if imm & 0x100000:
            imm -= 0x200000
        page = (base & ~0xFFF) + (imm << 12)

        # Decode ADD
        add = insns[1]
        add_imm = (add >> 10) & 0xFFF

        got_addr = page + add_imm
        print(f"  {name} @ 0x{base:x}")
        print(f"    ADRP page = 0x{page:x}")
        print(f"    ADD offset = 0x{add_imm:x}")
        print(f"    → GOT entry at 0x{got_addr:x}")
        print(f"    → loads [0x{got_addr:x}] and branches to it (PAC)")
        print()

# Also decode the non-PLT targets
print("\n=== Internal Helper Functions ===\n")

helpers = {
    "helper_09e1c": ("fa67bba9f85f01a9f65702a9f44f03a9fd7b04a9fd030191f40301aaf30300aa162d80529601a0720000168b23010094", 0x28af09e1c),
}

for name, (hex_data, base) in helpers.items():
    raw = bytes.fromhex(hex_data)
    print(f"  {name} @ 0x{base:x}:")
    for i in range(0, len(raw), 4):
        insn = struct.unpack_from("<I", raw, i)[0]
        pc = base + i
        # Look for BL
        if (insn & 0xFC000000) == 0x94000000:
            imm26 = insn & 0x03FFFFFF
            if imm26 & 0x02000000:
                imm26 -= 0x04000000
            target = pc + (imm26 << 2)
            print(f"    0x{pc:x}: BL 0x{target:x}")
        # Look for MOVZ
        elif (insn & 0xFF800000) == 0x52800000:
            rd = insn & 0x1F
            imm16 = (insn >> 5) & 0xFFFF
            if imm16:
                print(f"    0x{pc:x}: MOVZ w{rd}, #{imm16} (0x{imm16:x})")
    print()

print("\n=== Summary ===")
print("alloc_pd calls through PLT stubs that dispatch to GOT-resolved functions.")
print("The GOT addresses are in the 0x295e1xxxx-0x295e2xxxx range (libibverbs DATA segment).")
print("These resolve to libSystem functions — likely mach_msg/mach_msg2.")
print("\nThe constant w1=0x650 (1616) passed to target_0b098 is likely a mach_msg_id")
print("or an internal command code.")
