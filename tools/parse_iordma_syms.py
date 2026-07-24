#!/usr/bin/env python3
"""Parse IORDMAFamily exported symbols from decompressed kernelcache."""
import struct, mmap, sys

f = open("/tmp/vf_kexts/kc.bin", "rb")
data = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)

ncmds = struct.unpack_from("<I", data, 16)[0]
print(f"Kernelcache: {ncmds} load commands")

# Find IORDMAFamily
iordma_fileoff = None
offset = 32
for i in range(ncmds):
    cmd = struct.unpack_from("<I", data, offset)[0]
    cmdsize = struct.unpack_from("<I", data, offset + 4)[0]
    if cmd == 0x80000035:
        fileoff = struct.unpack_from("<Q", data, offset + 16)[0]
        eid = struct.unpack_from("<I", data, offset + 24)[0]
        name = data[offset + eid:].split(b"\x00")[0].decode()
        if name == "com.apple.iokit.IORDMAFamily":
            iordma_fileoff = fileoff
            print(f"IORDMAFamily at 0x{fileoff:x}")
    offset += cmdsize

if iordma_fileoff is None:
    print("NOT FOUND"); sys.exit(1)

# Parse symtab
sub_ncmds = struct.unpack_from("<I", data, iordma_fileoff + 16)[0]
sub_offset = iordma_fileoff + 32
symtab = None
for j in range(sub_ncmds):
    s_cmd = struct.unpack_from("<I", data, sub_offset)[0]
    s_cmdsize = struct.unpack_from("<I", data, sub_offset + 4)[0]
    if s_cmd == 0x2:
        symoff = struct.unpack_from("<I", data, sub_offset + 8)[0]
        nsyms = struct.unpack_from("<I", data, sub_offset + 12)[0]
        stroff = struct.unpack_from("<I", data, sub_offset + 16)[0]
        strsize = struct.unpack_from("<I", data, sub_offset + 20)[0]
        print(f"Symtab: {nsyms} syms at 0x{symoff:x}, strtab at 0x{stroff:x}")
        symtab = (symoff, nsyms, stroff, strsize)
    elif s_cmd == 0xB:
        iext = struct.unpack_from("<I", data, sub_offset + 16)[0]
        nxt = struct.unpack_from("<I", data, sub_offset + 20)[0]
        print(f"Dysymtab: {nxt} exported at index {iext}")
    sub_offset += s_cmdsize

if not symtab:
    print("No symtab"); sys.exit(1)

symoff, nsyms, stroff, strsize = symtab
strtab = data[stroff:stroff + strsize]

exported = []
for j in range(nsyms):
    so = symoff + j * 16
    if so + 16 > len(data): break
    n_strx = struct.unpack_from("<I", data, so)[0]
    n_type = data[so + 4]
    n_sect = data[so + 5]
    n_value = struct.unpack_from("<Q", data, so + 8)[0]
    if (n_type & 0x01) and n_sect != 0:
        end = strtab.find(b"\x00", n_strx)
        name = strtab[n_strx:end].decode("ascii", errors="replace") if end >= 0 else ""
        if name:
            exported.append((name, n_value))

print(f"\nExported+defined: {len(exported)}")

with open("/tmp/vf_kexts/iordma_exports.txt", "w") as out:
    for name, val in sorted(exported):
        out.write(f"{name}\n")

kws = ["ib_register", "ib_unregister", "ib_alloc_device", "ib_dealloc_device",
       "ib_query_device", "ib_query_port", "ib_get_port_immutable",
       "ib_alloc_pd", "ib_dealloc_pd", "ib_create_cq", "ib_destroy_cq",
       "ib_poll_cq", "ib_req_notify", "ib_create_qp", "ib_modify_qp",
       "ib_destroy_qp", "ib_post_send", "ib_post_recv",
       "ib_reg_user_mr", "ib_dereg_mr",
       "IORDMA", "uverbs", "rdma_create_ah"]
rdma = sorted(set((n, hex(v)) for n, v in exported if any(k in n for k in kws)))
print(f"\n=== Key symbols ({len(rdma)}) ===")
for name, val in rdma[:50]:
    print(f"  {val}  {name}")
