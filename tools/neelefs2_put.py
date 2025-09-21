#!/usr/bin/env python3
"""
Minimal NeeleFS v2 inserter: writes a file into an existing NeeleFS2 region
inside a raw disk image, creating parent directories as needed and updating
CRCs. Matches the on-disk layout used by drivers/fs/neelefs.c.

Usage:
  python3 tools/neelefs2_put.py <disk.img> </host/src/file> </fs/dest/path> [--lba 2048]

Notes:
- Only supports NeeleFS v2 (read-write). Superblock CRC is verified.
- Allocates a new contiguous extent; does not reclaim old space on overwrite.
- Directory entries have 32-byte names, no unicode normalization.

"""
import os, sys, struct, zlib

SECTOR = 512
MAGIC2 = b"NEELEFS2"
DIR_MAGIC = 0x454E3244  # 'D2NE' LE

def crc32(data: bytes, init=0):
    return zlib.crc32(data, init) & 0xFFFFFFFF

def rd(f, off, n):
    f.seek(off)
    b = f.read(n)
    if len(b) != n:
        raise RuntimeError("short read")
    return b

def wr(f, off, data: bytes):
    f.seek(off)
    f.write(data)

def load_super(f, lba):
    sec = rd(f, lba*SECTOR, SECTOR)
    if sec[:8] != MAGIC2:
        raise RuntimeError("not NeeleFS2 at LBA %d"%lba)
    # Verify CRC
    sb = bytearray(sec)
    stored = struct.unpack_from('<I', sb, 8+4+2+2+4+4+4)[0]
    struct.pack_into('<I', sb, 8+4+2+2+4+4+4, 0)
    calc = crc32(sb)
    if stored != calc:
        raise RuntimeError("superblock CRC mismatch")
    version, = struct.unpack_from('<I', sec, 8)
    if version != 2:
        raise RuntimeError("bad version")
    block_size, = struct.unpack_from('<H', sec, 12)
    total_blocks, bitmap_start, root_block = struct.unpack_from('<I I I', sec, 16)
    return {
        'lba': lba,
        'block_size': block_size,
        'total_blocks': total_blocks,
        'bitmap_start': bitmap_start,
        'root_block': root_block,
    }

def bitmap_get(f, sb, idx):
    byte = idx >> 3
    sec_lba = sb['lba'] + sb['bitmap_start'] + (byte >> 9)
    off = (byte & 0x1FF)
    sec = rd(f, sec_lba*SECTOR, SECTOR)
    return (sec[off] >> (idx & 7)) & 1

def bitmap_set(f, sb, idx, used):
    byte = idx >> 3
    sec_lba = sb['lba'] + sb['bitmap_start'] + (byte >> 9)
    off = (byte & 0x1FF)
    sec = bytearray(rd(f, sec_lba*SECTOR, SECTOR))
    if used: sec[off] |= (1 << (idx & 7))
    else:    sec[off] &= ~(1 << (idx & 7))
    wr(f, sec_lba*SECTOR, sec)

def alloc_contig(f, sb, nblocks):
    start = sb['bitmap_start'] + ( (sb['total_blocks'] + 4095)//4096 )
    run = 0; run_start = 0
    for i in range(start, sb['total_blocks']):
        if bitmap_get(f, sb, i) == 0:
            if run == 0: run_start = i
            run += 1
            if run >= nblocks:
                for b in range(nblocks): bitmap_set(f, sb, run_start+b, 1)
                return run_start
        else:
            run = 0
    return 0

def read_block_abs(f, abs_blk_idx):
    return rd(f, abs_blk_idx*SECTOR, SECTOR)

def write_block_abs(f, abs_blk_idx, data):
    if len(data) != SECTOR:
        raise RuntimeError("block must be 512 bytes")
    wr(f, abs_blk_idx*SECTOR, data)

def dir_load_header(sec):
    magic, next_block, entry_size, entries_per_blk, reserved = struct.unpack_from('<I I H H I', sec, 0)
    return magic, next_block, entry_size, entries_per_blk

def dir_init_block(f, sb, blk):
    sec = bytearray(SECTOR)
    struct.pack_into('<I I H H I', sec, 0, DIR_MAGIC, 0, 64, (SECTOR-16)//64, 0)
    write_block_abs(f, sb['lba'] + blk, sec)

def dir_find_entry(f, sb, dir_blk, name):
    blk = dir_blk
    name_bytes = name.encode('utf-8')[:32]
    name_padded = name_bytes + b'\x00'*(32-len(name_bytes))
    while blk != 0:
        sec = read_block_abs(f, sb['lba'] + blk)
        magic, next_blk, esz, cnt = dir_load_header(sec)
        base = 16 if magic == DIR_MAGIC else 0
        cnt  = cnt if magic == DIR_MAGIC else (SECTOR // 64)
        for i in range(cnt):
            off = base + i*64
            nm = sec[off:off+32]
            if nm[0] == 0: continue
            if nm == name_padded:
                ent = bytearray(sec[off:off+64])
                return (blk, off, ent)
        blk = next_blk if magic == DIR_MAGIC else 0
    return None

def dir_add_entry(f, sb, dir_blk, ent_bytes):
    blk = dir_blk
    while True:
        sec = bytearray(read_block_abs(f, sb['lba'] + blk))
        magic, next_blk, esz, cnt = dir_load_header(sec)
        if magic != DIR_MAGIC:
            # upgrade block to directory header
            struct.pack_into('<I I H H I', sec, 0, DIR_MAGIC, 0, 64, (SECTOR-16)//64, 0)
            magic, next_blk, esz, cnt = dir_load_header(sec)
        base = 16
        for i in range(cnt):
            off = base + i*64
            if sec[off] == 0:
                sec[off:off+64] = ent_bytes
                write_block_abs(f, sb['lba'] + blk, sec)
                return True
        # need to append
        if next_blk:
            blk = next_blk
            continue
        nb = alloc_contig(f, sb, 1)
        if nb == 0:
            return False
        dir_init_block(f, sb, nb)
        struct.pack_into('<I', sec, 4, nb)
        write_block_abs(f, sb['lba'] + blk, sec)
        # write new entry into the new block
        sec2 = bytearray(read_block_abs(f, sb['lba'] + nb))
        base2 = 16
        sec2[base2:base2+64] = ent_bytes
        write_block_abs(f, sb['lba'] + nb, sec2)
        return True

def ensure_path(f, sb, path):
    # returns block index of parent directory and final leaf name
    if not path.startswith('/'):
        raise RuntimeError("path must start with /")
    comps = [c for c in path.split('/') if c]
    if not comps:
        return sb['root_block'], ''
    dir_blk = sb['root_block']
    for c in comps[:-1]:
        found = dir_find_entry(f, sb, dir_blk, c)
        if found is None:
            # create dir
            nb = alloc_contig(f, sb, 1)
            if nb == 0: raise RuntimeError("no space for dir block")
            dir_init_block(f, sb, nb)
            name_bytes = c.encode('utf-8')[:32]
            name_p = name_bytes + b'\x00'*(32-len(name_bytes))
            ent = bytearray(64)
            ent[0:32] = name_p
            ent[32] = 2  # dir
            struct.pack_into('<I', ent, 36, nb)
            # size, csum, mtime left 0
            if not dir_add_entry(f, sb, dir_blk, ent):
                raise RuntimeError("failed to add dir entry")
            dir_blk = nb
        else:
            # assume it's a dir
            _, _, ent = found
            first_block, = struct.unpack_from('<I', ent, 36)
            dir_blk = first_block
    return dir_blk, comps[-1]

def put_file(f, sb, src_path, dest_path):
    parent_blk, leaf = ensure_path(f, sb, dest_path)
    if not leaf:
        raise RuntimeError("destination must be a file path")
    data = open(src_path,'rb').read()
    nblocks = (len(data) + SECTOR - 1)//SECTOR
    fb = alloc_contig(f, sb, nblocks)
    if fb == 0:
        raise RuntimeError("no space for file")
    # write data blocks
    off = 0
    for i in range(nblocks):
        chunk = data[off:off+SECTOR]
        if len(chunk) < SECTOR:
            chunk = chunk + b'\x00'*(SECTOR - len(chunk))
        write_block_abs(f, sb['lba'] + fb + i, chunk)
        off += SECTOR
    # dir entry
    name_b = leaf.encode('utf-8')[:32]
    name_p = name_b + b'\x00'*(32-len(name_b))
    ent = bytearray(64)
    ent[0:32] = name_p
    ent[32] = 1  # file
    struct.pack_into('<I I I', ent, 36, fb, len(data), crc32(data))
    # try overwrite existing, else add
    found = dir_find_entry(f, sb, parent_blk, leaf)
    if found is not None:
        blk, off = found[0], found[1]
        sec = bytearray(read_block_abs(f, sb['lba'] + blk))
        sec[off:off+64] = ent
        write_block_abs(f, sb['lba'] + blk, sec)
    else:
        if not dir_add_entry(f, sb, parent_blk, ent):
            raise RuntimeError("failed to add entry")

def main():
    if len(sys.argv) < 4:
        print("Usage: neelefs2_put.py <disk.img> <src_file> </dest/path> [--lba N]", file=sys.stderr)
        return 1
    img = sys.argv[1]
    src = sys.argv[2]
    dest = sys.argv[3]
    lba = 2048
    if len(sys.argv) >= 6 and sys.argv[4] == '--lba':
        lba = int(sys.argv[5])
    with open(img, 'r+b') as f:
        sb = load_super(f, lba)
        put_file(f, sb, src, dest)
    print(f"Wrote {src} to {img}:{dest}")
    return 0

if __name__ == '__main__':
    sys.exit(main())
