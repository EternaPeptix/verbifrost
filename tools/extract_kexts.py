#!/usr/bin/env python3
"""
Extract IORDMAFamily and AppleThunderboltRDMA kext binaries from the
running kernelcache. Uses the LC_FILESET_ENTRY load commands to locate
each kext's binary within the kernelcache file.

On macOS 15+, the kernelcache is a "fileset" Mach-O where each kext is
embedded as a sub-Mach-O accessible via LC_FILESET_ENTRY.

Build: Just run it. Needs read access to the kernelcache.
Usage: python3 extract_kexts.py
"""
import struct
import sys
import os

# Find the boot kernelcache (arm64e, prelinked).
# On macOS 15, the real kernelcache with all kexts is in the Preboot volume.
import glob
KC_PATHS = []
for p in glob.glob("/System/Volumes/Preboot/*/boot/*/System/Library/Caches/com.apple.kernelcaches/kernelcache"):
    KC_PATHS.append(p)
for p in glob.glob("/System/Library/Caches/com.apple.kernelcaches/kernelcache"):
    KC_PATHS.append(p)

def find_kc():
    for p in KC_PATHS:
        if os.path.exists(p):
            return p
    return None

def get_arm64e_slice(data):
    """Extract arm64e slice from fat Mach-O."""
    magic = struct.unpack_from(">I", data, 0)[0]  # fat magic is big-endian

    if magic == 0xCAFEBABF:  # FAT_MAGIC_64
        nfat = struct.unpack_from(">I", data, 4)[0]
        print(f"Fat binary with {nfat} architectures")
        for i in range(nfat):
            offset = 8 + i * 32
            cputype = struct.unpack_from(">I", data, offset)[0]
            cpusubtype = struct.unpack_from(">I", data, offset + 4)[0]
            offset_val = struct.unpack_from(">Q", data, offset + 8)[0]
            size = struct.unpack_from(">Q", data, offset + 16)[0]
            align = struct.unpack_from(">I", data, offset + 24)[0]
            print(f"  arch {i}: cputype=0x{cputype:x} subtype=0x{cpusubtype:x} "
                  f"offset={offset_val} size={size}")
            # arm64e: cputype=0x0100000C, subtype=0x80000002
            if cputype == 0x0100000C:
                print(f"  → arm64e slice at offset {offset_val}, size {size}")
                return data[offset_val:offset_val + size]
    elif magic == 0xFEEDFACF:  # Thin Mach-O
        print("Thin Mach-O (not fat)")
        return data
    else:
        print(f"Unknown magic: 0x{magic:x}")
        return None

def extract_kexts(data, output_dir):
    """Parse LC_FILESET_ENTRY commands and extract kext binaries."""
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic != 0xFEEDFACF:
        print(f"Not a Mach-O: magic=0x{magic:x}")
        return

    ncmds = struct.unpack_from("<I", data, 16)[0]
    print(f"\nParsing {ncmds} load commands...")

    entries = []
    offset = 32  # mach_header_64
    for i in range(ncmds):
        cmd = struct.unpack_from("<I", data, offset)[0]
        cmdsize = struct.unpack_from("<I", data, offset + 4)[0]

        if cmd == 0x80000035:  # LC_FILESET_ENTRY
            vmaddr = struct.unpack_from("<Q", data, offset + 8)[0]
            fileoff = struct.unpack_from("<Q", data, offset + 16)[0]
            entry_id_offset = struct.unpack_from("<I", data, offset + 24)[0]
            entry_name = data[offset + entry_id_offset:].split(b"\x00")[0].decode()

            if "RDMA" in entry_name:
                entries.append((entry_name, vmaddr, fileoff))

        offset += cmdsize

    print(f"Found {len(entries)} RDMA-related entries")

    # Calculate extent of each kext (from fileoff to next kext's fileoff or EOF)
    entries.sort(key=lambda x: x[2])

    for idx, (name, vmaddr, fileoff) in enumerate(entries):
        # Find the extent: scan this kext's load commands for the max file offset
        sub_ncmds = struct.unpack_from("<I", data, fileoff + 16)[0]
        max_file_end = 0
        sub_offset = fileoff + 32
        for j in range(sub_ncmds):
            s_cmd = struct.unpack_from("<I", data, sub_offset)[0]
            s_cmdsize = struct.unpack_from("<I", data, sub_offset + 4)[0]
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
        print("No kernelcache found!")
        sys.exit(1)

    print(f"Reading: {kc_path}")
    with open(kc_path, "rb") as f:
        raw = f.read()

    print(f"Size: {len(raw)} bytes")

    arm64e_slice = get_arm64e_slice(raw)
    if not arm64e_slice:
        print("Could not extract arm64e slice!")
        sys.exit(1)

    output_dir = "/tmp/vf_kexts"
    os.makedirs(output_dir, exist_ok=True)

    extract_kexts(arm64e_slice, output_dir)
    print(f"\nDone! Kexts extracted to {output_dir}/")

if __name__ == "__main__":
    main()
