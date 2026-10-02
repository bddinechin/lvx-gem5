#!/usr/bin/env python3
"""Compare the two runs of fp.c -- the host's and the ISS's -- word by word.

The ISS's stdout carries gem5's own banner before the program's bytes and its
exit line after, so the payload has to be cut out rather than compared whole;
that is all this does beyond the comparison itself.  A mismatch is reported as
the word index and the two bit patterns, because a float difference is only
legible in hex (0x7ff8000000000000 against 0xfff8000000000000 is a NaN's sign,
not a wrong answer).

Usage: fpdiff.py <host output> <iss stdout> <elf name as gem5 printed it>
"""
import struct
import sys

host_path, iss_path, elf = sys.argv[1], sys.argv[2], sys.argv[3]
host = open(host_path, 'rb').read()
iss = open(iss_path, 'rb').read()

marker = f'beginning execution of {elf} ==\n'.encode()
start = iss.find(marker)
if start < 0:
    sys.exit(f'fpdiff: the ISS never started {elf} -- no banner in its output')
body = iss[start + len(marker):]
end = body.rfind(b'== Exiting')
if end < 0:
    sys.exit('fpdiff: the ISS did not reach its exit line')
body = body[:end]

if len(body) != len(host):
    sys.exit(f'fpdiff: {len(host)} bytes from the host, {len(body)} from the ISS'
             ' -- one of them stopped early')

words = len(host) // 8
bad = [k for k in range(words)
       if host[8 * k:8 * k + 8] != body[8 * k:8 * k + 8]]
if not bad:
    print(f'fp: identical, {words} words ({len(host)} bytes)')
    sys.exit(0)

print(f'fp: DIFFER in {len(bad)} of {words} words')
for k in bad[:20]:
    a, = struct.unpack('<Q', host[8 * k:8 * k + 8])
    b, = struct.unpack('<Q', body[8 * k:8 * k + 8])
    print(f'  [{k}] host={a:#018x} iss={b:#018x}')
if len(bad) > 20:
    print(f'  ... and {len(bad) - 20} more')
sys.exit(1)
