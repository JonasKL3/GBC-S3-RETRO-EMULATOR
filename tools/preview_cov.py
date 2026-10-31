"""Decode an existing NS3C cover without altering its color, contrast or aspect."""
import argparse
import pathlib
import struct
import zlib

def decode(data):
    if len(data)!=28300 or data[:12]!=struct.pack('<4sHHHH',b'NS3C',104,136,1,0):
        raise ValueError('Expected NS3C v1, 104x136, reserved=0, 28300 bytes')
    raw=bytearray()
    for row in range(136):
        raw.append(0)  # PNG scanline filter: none
        for col in range(104):
            c=struct.unpack_from('<H',data,12+(row*104+col)*2)[0]
            r,g,b=c>>11,(c>>5)&63,c&31
            raw.extend(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)))
    def chunk(kind,payload):
        return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',104,136,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cover',type=pathlib.Path)
    parser.add_argument('png',type=pathlib.Path)
    args=parser.parse_args()
    if args.cover.resolve()==args.png.resolve():parser.error('Choose a separate PNG destination')
    result=decode(args.cover.read_bytes());args.png.write_bytes(result)
    print(str(args.png)+' — exact decoded RGB565; no highlight correction')
