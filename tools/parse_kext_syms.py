#!/usr/bin/env python3
"""
Parse the Mach-O symbol table from extracted kext binaries.
Shows exported symbols that other kexts can link against.
"""
import struct
import sys

def parse_macho_symbols(filepath):
    with open(filepath, "rb") as f:
        data = f.read()

    magic = struct.unpack_from("<I", data, 0)[0]
    if magic != 0xFEEDFACF:
        print(f"Not a 64-bit Mach-O: 0x{magic:x}")
        return

    ncmds = struct.unpack_from("<I", data, 16)[0]
    segments = {}
    symtab_info = None
    dysymtab_info = None

    offset = 32
    for i in range(ncmds):
        cmd = struct.unpack_from("<I", data, offset)[0]
        cmdsize = struct.unpack_from("<I", data, offset + 4)[0]

        if cmd == 0x19:  # LC_SEGMENT_64
            segname = data[offset+8:offset+24].split(b"\x00")[0].decode()
            vmaddr = struct.unpack_from("<Q", data, offset + 40)[0]
            fileoff = struct.unpack_from("<Q", data, offset + 56)[0]
            filesize = struct.unpack_from("<Q", data, offset + 64)[0]
            segments[segname] = {"vmaddr": vmaddr, "fileoff": fileoff, "filesize": filesize}

        elif cmd == 0x2:  # LC_SYMTAB
            symoff = struct.unpack_from("<I", data, offset + 8)[0]
            nsyms = struct.unpack_from("<I", data, offset + 12)[0]
            stroff = struct.unpack_from("<I", data, offset + 16)[0]
            strsize = struct.unpack_from("<I", data, offset + 20)[0]
            symtab_info = {"symoff": symoff, "nsyms": nsyms, "stroff": stroff, "strsize": strsize}

        elif cmd == 0xB:  # LC_DYSYMTAB
            iextdefsym = struct.unpack_from("<I", data, offset + 16)[0]
            nextdefsym = struct.unpack_from("<I", data, offset + 20)[0]
            dysymtab_info = {"iextdefsym": iextdefsym, "nextdefsym": nextdefsym}

        offset += cmdsize

    print(f"Segments: {list(segments.keys())}")
    print(f"Symtab: {symtab_info}")

    if not symtab_info:
        print("No symbol table!")
        return

    text_seg = segments.get("__TEXT")
    # In kernelcache fileset entries, the fileoff fields are absolute
    # offsets into the decompressed kernelcache. The extracted binary
    # starts at the first segment's fileoff. So we subtract the minimum
    # fileoff (which is __TEXT's fileoff).
    all_fileoffs = [s["fileoff"] for s in segments.values()]
    base_fileoff = min(all_fileoffs)

    adj_symoff = symtab_info["symoff"] - base_fileoff
    adj_stroff = symtab_info["stroff"] - base_fileoff

    print(f"Base fileoff: {base_fileoff}")
    print(f"Adjusted symoff: {adj_symoff} (0x{adj_symoff:x})")
    print(f"Adjusted stroff: {adj_stroff} (0x{adj_stroff:x})")

    if adj_symoff < 0 or adj_symoff >= len(data):
        print(f"symoff out of range for {len(data)}-byte file")
        # The kernelcache is 119MB; our extracted file may be smaller.
        # The symbol table may be beyond our extraction boundary.
        # Try reading from the full decompressed kernelcache instead.
        print("Symbol table beyond extracted file boundary.")
        print("Need to re-extract with proper LINKEDIT extent.")
        return

    strtab = data[adj_stroff:adj_stroff + symtab_info["strsize"]]

    exported = []
    for j in range(symtab_info["nsyms"]):
        so = adj_symoff + j * 16
        if so + 16 > len(data):
            break
        n_strx = struct.unpack_from("<I", data, so)[0]
        n_type = data[so + 4]
        n_value = struct.unpack_from("<Q", data, so + 8)[0]

        name_end = strtab.find(b"\x00", n_strx)
        name = strtab[n_strx:name_end].decode("ascii", errors="replace") if name_end >= 0 else ""

        is_external = (n_type & 0x01) != 0
        if is_external and name:
            exported.append((name, n_value))

    print(f"\nExported symbols: {len(exported)}")

    # Categorize
    categories = {
        "Registration": ["ib_register", "ib_unregister", "ib_alloc_device",
                         "ib_dealloc_device", "rdma_register"],
        "Device Ops": ["ib_query_device", "ib_query_port", "ib_get_port_immutable",
                       "ib_modify_device", "ib_get_dev_fw_str"],
        "PD": ["ib_alloc_pd", "ib_dealloc_pd"],
        "CQ": ["ib_create_cq", "ib_destroy_cq", "ib_poll_cq", "ib_req_notify_cq"],
        "QP": ["ib_create_qp", "ib_modify_qp", "ib_destroy_qp", "ib_post_send",
               "ib_post_recv", "ib_query_qp"],
        "MR": ["ib_reg_user_mr", "ib_dereg_mr", "ib_reg_mr", "ib_map_mr_sg"],
        "AH": ["ib_create_ah", "ib_destroy_ah", "rdma_create_ah"],
        "uverbs": ["ib_uverbs", "uverbs_"],
        "IORDMA": ["IORDMA"],
    }

    for cat, keywords in categories.items():
        syms = [(n, v) for n, v in exported
                if any(k in n for k in keywords)]
        if syms:
            print(f"\n=== {cat} ({len(syms)} symbols) ===")
            for name, val in sorted(syms)[:30]:
                print(f"  0x{val:016x}  {name}")

    # Show ALL if few
    if len(exported) < 100:
        print(f"\n=== ALL exported symbols ===")
        for name, val in sorted(exported):
            print(f"  0x{val:016x}  {name}")

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/vf_kexts/IORDMAFamily.kext.bin"
    parse_macho_symbols(path)
