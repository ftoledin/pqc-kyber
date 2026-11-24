#!/usr/bin/env python3
"""Compare the integrated CHF1 core with an unmodified upstream Q32.32 source.

The caller provides a local checkout of hash_options/chaotic_hash_32.c. Only a
temporary shared library is compiled; no downloads or source edits are made.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


class Context(ctypes.Structure):
    _fields_ = [(name, ctypes.c_int64) for name in
                ('z_real', 'z_imag', 'c_real', 'c_imag', 'alpha')] + [('h_size', ctypes.c_int)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--upstream-source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    pointer = ctypes.POINTER(ctypes.c_uint8)
    ctx_pointer = ctypes.POINTER(Context)

    def configure(lib, framed):
        lib.ChaoticHashInit.argtypes = [ctx_pointer, ctypes.c_int]
        lib.ChaoticHashInit.restype = None
        lib.ChaoticHashing.argtypes = [ctx_pointer, pointer, pointer, ctypes.c_size_t]
        lib.ChaoticHashing.restype = None
        if framed:
            lib.ChaoticHashingDomain.argtypes = [ctx_pointer, pointer, pointer, ctypes.c_size_t, ctypes.c_uint32]
            lib.ChaoticHashingDomain.restype = None
        return lib

    def buffer(data):
        result = (ctypes.c_uint8 * max(1, len(data)))()
        if data:
            ctypes.memmove(result, data, len(data))
        return result

    def state(ctx):
        return tuple(getattr(ctx, field) for field, _ in Context._fields_)

    with tempfile.TemporaryDirectory(prefix='kyber-upstream-reference-') as directory:
        target = Path(directory) / 'reference.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', str(args.upstream_source.resolve()),
                        '-o', str(target)], check=True, capture_output=True, text=True)
        reference = configure(ctypes.CDLL(str(target)), False)
        local = configure(ctypes.CDLL(str(args.library.resolve())), True)
        absorption_checks = extraction_checks = 0
        for size in (0, 1, 4, 5, 7, 8, 9, 16, 31, 32, 33, 34, 63, 64, 65, 129,
                     768, 1024, 1088, 1184, 1568, 65536):
            for case in range(2):
                data = hashlib.shake_256(f'upstream-comparison-{size}-{case}'.encode()).digest(size)
                for bits in (128, 256, 512):
                    for domain in range(7):
                        # Build the canonical input independently of C's virtual-word reader.
                        encoded = (b'CHF\x01' + domain.to_bytes(4, 'little') + data +
                                   bytes(-len(data) % 8) + len(data).to_bytes(8, 'little'))
                        a, b = Context(), Context()
                        local.ChaoticHashInit(ctypes.byref(a), bits)
                        reference.ChaoticHashInit(ctypes.byref(b), bits)
                        local.ChaoticHashingDomain(ctypes.byref(a), None, buffer(data), len(data), domain)
                        reference.ChaoticHashing(ctypes.byref(b), None, buffer(encoded), len(encoded))
                        assert state(a) == state(b), ('absorption', size, case, bits, domain)
                        absorption_checks += 1
                        # Continue squeezing from both states, including multiple calls.
                        for out_bits in (32, 128, 512):
                            a.h_size = b.h_size = out_bits
                            left, right = buffer(bytes(out_bits // 8)), buffer(bytes(out_bits // 8))
                            local.ChaoticHashing(ctypes.byref(a), left, None, 0)
                            reference.ChaoticHashing(ctypes.byref(b), right, None, 0)
                            assert bytes(left) == bytes(right) and state(a) == state(b), (
                                'extraction', size, case, bits, domain, out_bits)
                            extraction_checks += 1
        # Record why the upstream input format cannot replace CHF1 unchanged.
        padding = []
        for bits in (256, 512):
            hashes = []
            for data in (b'A', b'A\0'):
                ctx = Context()
                reference.ChaoticHashInit(ctypes.byref(ctx), bits)
                out = buffer(bytes(bits // 8))
                reference.ChaoticHashing(ctypes.byref(ctx), out, buffer(data), len(data))
                hashes.append(bytes(out))
            padding.append(dict(hash_bits=bits, inputs_hex=['41', '4100'], same_hash=hashes[0] == hashes[1]))
        report = dict(absorption_checks=absorption_checks, extraction_checks=extraction_checks,
                      failures=0, raw_upstream_padding_observations=padding,
                      upstream_source_sha256=hashlib.sha256(args.upstream_source.read_bytes()).hexdigest(),
                      integrated_library_sha256=hashlib.sha256(args.library.read_bytes()).hexdigest())
        args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report))


if __name__ == '__main__':
    main()
