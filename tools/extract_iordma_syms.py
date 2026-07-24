#!/usr/bin/env python3
"""
Extract the IORDMAFamily symbol table directly from the decompressed
kernelcache. This avoids the file-offset translation problem.

We decompress the full kernelcache, find the IORDMAFamily fileset entry,
then read the LC_SYMTAB (symoff/stroff are absolute kernelcache offsets).
"""
import struct
import sys
import glob

def find_kc():
    for p in glob.glob("/System/Volumes/Preboot/*/boot/*/System/Library/Caches/com.apple.kernelcaches/kernelcache"):
        return p
    return None

def decompress_kc(kc_path):
    import pyimg4
    print(f"Reading: {kc_path}")
    with open(kc_path, "rb") as f:
        raw = f.read()
    print(f"Compressed: {len(raw)} bytes")
    img = pyimg4.IMG4(raw)
    payload = img.im4p.payload
    payload.decompress()
    # Write to disk for memory-efficient parsing
    with open("/tmp/vf_kexts/kernelcache.decompressed", "wb") as f:
        f.write(payload.data)
    print(f"Decompressed: {len(payload.data)} bytes → /tmp/vf_kexts/kernelcache.decompressed")
    del payload, img, raw
    # Read back with mmap
    import mmap
    f = open("/tmp/vf_kexts/kernelcache.decompressed", "rb")
    return mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)

def find_iordma(data):
    """Find the IORDMAFamily fileset entry and its LC_SYMTAB."""
    ncmds = struct.unpack_from("<I", data, 16)[0]

    iordma_fileoff = None
    iordma_ncmds = None

    offset = 32
    for i in range(ncmds):
        cmd = struct.unpack_from("<I", data, offset)[0]
        cmdsize = struct.unpack_from("<I", data, offset + 4)[0]
        if cmdsize == 0:
            break

        if cmd == 0x80000035:  # LC_FILESET_ENTRY
            fileoff = struct.unpack_from("<Q", data, offset + 16)[0]
            entry_id_offset = struct.unpack_from("<I", data, offset + 24)[0]
            name = data[offset + entry_id_offset:].split(b"\x00")[0].decode()
            if name == "com.apple.iokit.IORDMAFamily":
                iordma_fileoff = fileoff
                iordma_ncmds = struct.unpack_from("<I", data, fileoff + 16)[0]
                print(f"\nIORDMAFamily at fileoff {fileoff} (0x{fileoff:x})")

        offset += cmdsize

    if iordma_fileoff is None:
        print("IORDMAFamily not found!")
        return None

    # Parse IORDMAFamily's load commands to find LC_SYMTAB
    sub_offset = iordma_fileoff + 32
    symtab = None
    for j in range(iordma_ncmds):
        s_cmd = struct.unpack_from("<I", data, sub_offset)[0]
        s_cmdsize = struct.unpack_from("<I", data, sub_offset + 4)[0]
        if s_cmdsize == 0:
            break

        if s_cmd == 0x2:  # LC_SYMTAB
            symoff = struct.unpack_from("<I", data, sub_offset + 8)[0]
            nsyms = struct.unpack_from("<I", data, sub_offset + 12)[0]
            stroff = struct.unpack_from("<I", data, sub_offset + 16)[0]
            strsize = struct.unpack_from("<I", data, sub_offset + 20)[0]
            symtab = {"symoff": symoff, "nsyms": nsyms,
                      "stroff": stroff, "strsize": strsize}
            print(f"LC_SYMTAB: symoff={symoff} nsyms={nsyms} "
                  f"stroff={stroff} strsize={strsize}")

        elif s_cmd == 0xB:  # LC_DYSYMTAB
            iextdefsym = struct.unpack_from("<I", data, sub_offset + 16)[0]
            nextdefsym = struct.unpack_from("<I", data, sub_offset + 20)[0]
            print(f"LC_DYSYMTAB: iextdefsym={iextdefsym} nextdefsym={nextdefsym}")

        sub_offset += s_cmdsize

    return data, symtab

def parse_symbols(data, symtab):
    """Read exported symbols from the kernelcache symbol table."""
    strtab = data[symtab["stroff"]:symtab["stroff"] + symtab["strsize"]]

    exported = []
    for j in range(symtab["nsyms"]):
        so = symtab["symoff"] + j * 16
        if so + 16 > len(data):
            break
        n_strx = struct.unpack_from("<I", data, so)[0]
        n_type = data[so + 4]
        n_value = struct.unpack_from("<Q", data, so + 8)[0]

        name_end = strtab.find(b"\x00", n_strx)
        name = strtab[n_strx:name_end].decode("ascii", errors="replace") if name_end >= 0 else ""

        # N_EXT bit (0x01) means externally visible
        if (n_type & 0x01) and name:
            # Filter: skip undefined symbols (n_sect == 0 means undefined)
            n_sect = data[so + 5]
            if n_sect != 0:  # defined symbol
                exported.append((name, n_value))

    print(f"\nTotal defined+external symbols: {len(exported)}")

    # Save all to file
    with open("/tmp/vf_kexts/iordma_exports.txt", "w") as f:
        for name, val in sorted(exported):
            f.write(f"0x{val:016x} {name}\n")

    # Show key categories
    keywords_map = {
        "Registration": ["ib_register", "ib_unregister", "ib_alloc_device",
                         "ib_dealloc_device", "rdma_register"],
        "DeviceOps": ["ib_query_device", "ib_query_port", "ib_get_port"],
        "PD": ["ib_alloc_pd", "ib_dealloc_pd"],
        "CQ": ["ib_create_cq", "ib_destroy_cq", "ib_poll_cq", "ib_req_notify"],
        "QP": ["ib_create_qp", "ib_modify_qp", "ib_destroy_qp",
               "ib_post_send", "ib_post_recv"],
        "MR": ["ib_reg_user_mr", "ib_dereg_mr", "ib_reg_mr"],
        "IORDMA": ["IORDMA"],
        "Uverbs": ["ib_uverbs", "uverbs_"],
    }

    for cat, kws in keywords_map.items():
        syms = [(n, v) for n, v in exported if any(k in n for k in kws)]
        if syms:
            print(f"\n=== {cat} ({len(syms)}) ===")
            for name, val in sorted(syms)[:20]:
                print(f"  0x{val:016x}  {name}")

if __name__ == "__main__":
    kc_path = find_kc()
    if not kc_path:
        print("No kernelcache found!")
        sys.exit(1)
    data = decompress_kc(kc_path)
    result = find_iordma(data)
    if result:
        _, symtab = result
        parse_symbols(data, symtab)
