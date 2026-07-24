#!/usr/bin/env python3
"""
Extract IORDMAFamily and AppleThunderboltRDMA kext binaries from the
boot kernelcache. Uses pyimg4 to decompress the IM4P container, then
parses LC_FILESET_ENTRY to locate and extract each kext.
"""
import struct
import sys
import os
import glob

def find_kc():
    for p in glob.glob("/System/Volumes/Preboot/*/boot/*/System/Library/Caches/com.apple.kernelcaches/kernelcache"):
        return p
    return None

def decompress_kc(kc_path):
    """Decompress IM4P kernelcache using pyimg4."""
    import pyimg4
    print(f"Reading: {kc_path}")
    with open(kc_path, "rb") as f:
        raw = f.read()
    print(f"Compressed size: {len(raw)} bytes")

    # Parse as IMG4, decompress LZFSE payload
    img = pyimg4.IMG4(raw)
    print(f"IMG4 fourcc: {img.im4p.fourcc}")
    print(f"Description: {img.im4p.description}")
    print(f"Compression: {img.im4p.payload.compression}")

    payload = img.im4p.payload
    payload.decompress()
    data = payload.data

    print(f"Decompressed size: {len(data)} bytes")
    magic = struct.unpack_from("<I", data, 0)[0]
    print(f"Magic: 0x{magic:x} (feedfacf = Mach-O)")
    return data

def extract_kexts(data, output_dir):
    """Parse LC_FILESET_ENTRY and extract RDMA kexts."""
    ncmds = struct.unpack_from("<I", data, 16)[0]
    print(f"\nParsing {ncmds} load commands...")

    entries = []
    offset = 32
    for i in range(ncmds):
        cmd = struct.unpack_from("<I", data, offset)[0]
        cmdsize = struct.unpack_from("<I", data, offset + 4)[0]
        if cmdsize == 0:
            break

        if cmd == 0x80000035:  # LC_FILESET_ENTRY
            vmaddr = struct.unpack_from("<Q", data, offset + 8)[0]
            fileoff = struct.unpack_from("<Q", data, offset + 16)[0]
            entry_id_offset = struct.unpack_from("<I", data, offset + 24)[0]
            entry_name = data[offset + entry_id_offset:].split(b"\x00")[0].decode()

            if "RDMA" in entry_name:
                entries.append((entry_name, vmaddr, fileoff))

        offset += cmdsize

    print(f"Found {len(entries)} RDMA-related entries")

    entries.sort(key=lambda x: x[2])

    for name, vmaddr, fileoff in entries:
        sub_ncmds = struct.unpack_from("<I", data, fileoff + 16)[0]
        max_file_end = 0
        sub_offset = fileoff + 32
        for j in range(sub_ncmds):
            if sub_offset + 8 > len(data):
                break
            s_cmd = struct.unpack_from("<I", data, sub_offset)[0]
            s_cmdsize = struct.unpack_from("<I", data, sub_offset + 4)[0]
            if s_cmdsize == 0:
                break
            if s_cmd == 0x19:  # LC_SEGMENT_64
                seg_fileoff = struct.unpack_from("<Q", data, sub_offset + 48)[0]
                seg_filesize = struct.unpack_from("<Q", data, sub_offset + 56)[0]
                end = seg_fileoff + seg_filesize
                if end > max_file_end:
                    max_file_end = end
            sub_offset += s_cmdsize

        kext_data = data[fileoff:max_file_end]
        short_name = name.replace("com.apple.iokit.", "").replace("com.apple.driver.", "")
        outpath = os.path.join(output_dir, f"{short_name}.kext.bin")

        with open(outpath, "wb") as f:
            f.write(kext_data)

        print(f"\n  Extracted: {short_name}")
        print(f"    Entry: {name}")
        print(f"    vmaddr: 0x{vmaddr:x}")
        print(f"    fileoff: {fileoff} (0x{fileoff:x})")
        print(f"    size: {len(kext_data)} bytes")
        print(f"    saved: {outpath}")

    return entries

def main():
    kc_path = find_kc()
    if not kc_path:
        print("No boot kernelcache found!")
        sys.exit(1)

    decompressed = decompress_kc(kc_path)

    output_dir = "/tmp/vf_kexts"
    os.makedirs(output_dir, exist_ok=True)
    extract_kexts(decompressed, output_dir)
    print(f"\nDone! Kexts extracted to {output_dir}/")

if __name__ == "__main__":
    main()
