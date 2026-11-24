#!/usr/bin/env python3
"""Build/inspect Vinyl Dual bundles; images checked before producing an update."""
import argparse,hashlib,struct
from pathlib import Path
LIMIT=0x180000

def image(data,chip):
    if not 24<=len(data)<=LIMIT or data[0]!=0xe9 or struct.unpack_from('<H',data,12)[0]!=chip:
        raise ValueError(f'Sai image hoặc chip ID {chip}')

def pack(audio,display):
    image(audio,0);image(display,5)
    header=bytearray(256);header[:8]=b'VINYLUP1';struct.pack_into('<II',header,8,len(audio),len(display))
    header[16:48]=hashlib.md5(audio).hexdigest().encode();header[48:80]=hashlib.md5(display).hexdigest().encode()
    return bytes(header)+audio+display

def inspect(data):
    if len(data)<256 or data[:8]!=b'VINYLUP1': raise ValueError('Sai header bundle')
    a,b=struct.unpack_from('<II',data,8)
    if len(data)!=256+a+b: raise ValueError('Sai kích thước bundle')
    audio,display=data[256:256+a],data[256+a:];image(audio,0);image(display,5)
    if data[16:48]!=hashlib.md5(audio).hexdigest().encode() or data[48:80]!=hashlib.md5(display).hexdigest().encode():
        raise ValueError('Sai MD5 bundle')
    return {'audio':len(audio),'display':len(display),'sha256':hashlib.sha256(data).hexdigest()}

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--audio',type=Path);parser.add_argument('--display',type=Path);parser.add_argument('--output',type=Path);parser.add_argument('--inspect',type=Path)
    args=parser.parse_args()
    if args.inspect: print(inspect(args.inspect.read_bytes()))
    elif args.audio and args.display and args.output:
        data=pack(args.audio.read_bytes(),args.display.read_bytes());args.output.write_bytes(data);print(inspect(data))
    else: parser.error('Dùng --audio app.bin --display app.bin --output bundle.vup hoặc --inspect bundle.vup')
