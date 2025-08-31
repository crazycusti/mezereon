#!/usr/bin/env python3
import os, sys, struct

MAGIC=b"NEELEFS1"
ENTRY_SIZE=32+4+4+4  # name(32) + offset + size + checksum

def pack_dir(srcdir):
    entries=[]
    for name in sorted(os.listdir(srcdir)):
        path=os.path.join(srcdir,name)
        if not os.path.isfile(path):
            continue
        size=os.path.getsize(path)
        entries.append((name,path,size))
    return entries

def align_up(x,a):
    return (x + a - 1) & ~(a-1)

def main():
    if len(sys.argv)<3:
        print("Usage: mkneelefs.py <src_dir> <out.img>")
        return 1
    src=sys.argv[1]
    out=sys.argv[2]
    ents=pack_dir(src)
    count=len(ents)
    table_bytes=count*ENTRY_SIZE
    header=struct.pack('<8sII',MAGIC,count,table_bytes)
    data_start=align_up(16+table_bytes,512)

    # Build entries with computed offsets
    cur=data_start
    entry_bytes=b''
    file_data=[]
    for name,path,size in ents:
        n=name.encode('utf-8')[:31]
        n=n+b'\x00'*(32-len(n))
        offset=cur
        checksum=0
        entry_bytes+=n+struct.pack('<III',offset,size,checksum)
        with open(path,'rb') as f:
            data=f.read()
        file_data.append(data)
        cur+=len(data)

    with open(out,'wb') as f:
        f.write(header)
        f.write(entry_bytes)
        # pad to data_start
        pad_len=data_start - (16+len(entry_bytes))
        if pad_len<0:
            raise SystemExit("table larger than expected")
        f.write(b'\x00'*pad_len)
        # write data
        for data in file_data:
            f.write(data)
        # final pad to 512
        final_pad = (-f.tell()) % 512
        if final_pad:
            f.write(b'\x00'*final_pad)
    print(f"Wrote {out} with {count} files, size {os.path.getsize(out)} bytes")

if __name__=='__main__':
    sys.exit(main())

