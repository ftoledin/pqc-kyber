#!/usr/bin/env python3
"""Additional reproducible chaotic-hash checks; findings do not modify the core."""
import argparse
import ctypes
from datetime import date
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    lib = ctypes.CDLL(str(args.library.resolve()))
    def digest(data, bits):
        data = bytes(data)
        out = (ctypes.c_uint8 * (bits // 8))()
        src = (ctypes.c_uint8 * max(1, len(data)))()
        if data:
            ctypes.memmove(src, data, len(data))
        function = getattr(lib, f'_sha3_{bits}')
        function.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
        function.restype = None
        function(out, src, len(data))
        return bytes(out)
    reports = []
    for bits in (256, 512):
        padding = []
        for n in range(1, 65):
            a = hashlib.shake_256(f'audit-padding-{n}'.encode()).digest(n)
            b = a + b'\0'
            if digest(a, bits) == digest(b, bits):
                padding.append(dict(length=n, original_hex=a.hex(), changed_hex=b.hex(), digest=digest(a, bits).hex()))
        sentinel = []
        for n in range(4, 65, 8):
            a = hashlib.shake_256(f'audit-sentinel-{n}'.encode()).digest(n)
            b = a + b'\1'
            if digest(a, bits) == digest(b, bits):
                sentinel.append(dict(length=n, original_hex=a.hex(), changed_hex=b.hex()))
        changed_bits = cases = 0
        same_length_collisions = []
        inputs = [bytes(range(64))] + [hashlib.shake_256(f'audit-avalanche-{i}-{n}'.encode()).digest(n)
                                     for n in (32, 64) for i in range(4)]
        for index, data in enumerate(inputs):
            expected = digest(data, bits)
            for bit in range(len(data) * 8):
                changed = bytearray(data)
                changed[bit // 8] ^= 1 << (bit % 8)
                actual = digest(changed, bits)
                cases += 1
                changed_bits += sum((a ^ b).bit_count() for a, b in zip(actual, expected))
                if expected == actual:
                    same_length_collisions.append(dict(input_index=index, byte=bit // 8, bit=bit % 8,
                        original_hex=data.hex(), changed_hex=bytes(changed).hex(), digest=expected.hex()))
        examples = []
        for a, b in [(b'A', b'A\0'), (b'ABCD', b'ABCD\1'), (b'abcde', b'abcde\0')]:
            examples.append(dict(original_hex=a.hex(), changed_hex=b.hex(), same=digest(a, bits) == digest(b, bits),
                                 digest=digest(a, bits).hex()))
        reports.append(dict(hash_bits=bits, zero_append_cases=64, zero_append_collisions=padding,
            sentinel_append_cases=8, sentinel_append_collisions=sentinel, single_bit_cases=cases,
            single_bit_collisions=same_length_collisions, mean_changed_bits=changed_bits / cases,
            mean_changed_fraction=changed_bits / cases / bits, examples=examples))
    result = dict(date=date.today().isoformat(), precision=32, framing='CHF1', results=reports,
                  library_sha256=hashlib.sha256(args.library.read_bytes()).hexdigest())
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps([dict(bits=r['hash_bits'], zero_append_collisions=len(r['zero_append_collisions']),
        sentinel_append_collisions=len(r['sentinel_append_collisions']), single_bit_cases=r['single_bit_cases'],
        single_bit_collisions=len(r['single_bit_collisions']), mean_changed_fraction=r['mean_changed_fraction'])
        for r in reports]))
    raise SystemExit(any(r['zero_append_collisions'] or r['sentinel_append_collisions'] or
                        r['single_bit_collisions'] for r in reports))


if __name__ == '__main__':
    main()
