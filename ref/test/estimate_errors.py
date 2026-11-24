#!/usr/bin/env python3
"""Reproducible KEM mutation experiment; test coins are not production randomness."""
import argparse
import ctypes
import hashlib
import json
import math
from pathlib import Path


def interval(errors, samples):
    z = 1.959963984540054
    p = errors / samples
    divisor = 1 + z * z / samples
    center = (p + z * z / (2 * samples)) / divisor
    half = z * math.sqrt(p * (1 - p) / samples + z * z / (4 * samples**2)) / divisor
    return [max(0.0, center - half), min(1.0, center + half)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--variant', type=int, choices=(512, 768, 1024), required=True)
    parser.add_argument('--precision', type=int, choices=(24, 32), required=True,
                        help='CHAOTIC_FRAC_BITS used to compile the supplied library')
    parser.add_argument('--sessions', type=int, default=10000)
    parser.add_argument('--seed', default='kyber-q24-q32-20261020')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--extended', action='store_true', help='Also test two distinct bits and a random full ciphertext')
    args = parser.parse_args()
    if args.sessions < 1:
        parser.error('--sessions must be positive')
    sizes = {512: (800, 1632, 768), 768: (1184, 2400, 1088), 1024: (1568, 3168, 1568)}
    pk_size, sk_size, ct_size = sizes[args.variant]
    lib = ctypes.CDLL(str(args.library.resolve()))
    pointer = ctypes.POINTER(ctypes.c_uint8)
    prefix = f'pqcrystals_kyber{args.variant}_ref_'
    def function(name, count):
        result = getattr(lib, prefix + name)
        result.argtypes = [pointer] * count
        result.restype = None if name == 'kyber_shake256_rkprf' else ctypes.c_int
        return result
    keypair, enc, dec, reject = (function('keypair_derand', 3), function('enc_derand', 4),
                                 function('dec', 3), function('kyber_shake256_rkprf', 3))
    def buffer(data_or_size):
        if isinstance(data_or_size, int):
            return (ctypes.c_uint8 * data_or_size)()
        return (ctypes.c_uint8 * len(data_or_size)).from_buffer_copy(data_or_size)
    def draw(trial, domain, length):
        return hashlib.shake_256(args.seed.encode() + b'\0' + trial.to_bytes(8, 'little') + domain).digest(length)
    def uniform(trial, domain, bound):
        # Rejection sampling avoids modulo bias and is independent of the chaotic hash.
        counter, threshold = 0, (1 << 32) % bound
        while True:
            value = int.from_bytes(draw(trial, domain + counter.to_bytes(4, 'little'), 4), 'little')
            if value >= threshold:
                return value % bound
            counter += 1
    counts = dict(valid_fail=0, bit_same=0, bit_nonreject=0, byte_same=0, byte_nonreject=0)
    if args.extended:
        counts.update(twobit_same=0, twobit_nonreject=0, random_same=0, random_nonreject=0)
    regions = dict(u=dict(trials=0, nonreject=0), v=dict(trials=0, nonreject=0))
    v_bytes = 160 if args.variant == 1024 else 128
    examples = []
    for trial in range(args.sessions):
        coins = draw(trial, b'coins', 96)
        pk, sk, ct = buffer(pk_size), buffer(sk_size), buffer(ct_size)
        expected, actual, fallback = buffer(32), buffer(32), buffer(32)
        assert keypair(pk, sk, buffer(coins[:64])) == 0
        assert enc(ct, expected, pk, buffer(coins[64:])) == 0
        assert dec(actual, ct, sk) == 0
        counts['valid_fail'] += bytes(actual) != bytes(expected)
        bit = uniform(trial, b'bit', ct_size * 8)
        pos = uniform(trial, b'byte', ct_size)
        mask = uniform(trial, b'mask', 255) + 1
        mutations = [('bit', [(bit // 8, 1 << (bit % 8))]), ('byte', [(pos, mask)])]
        if args.extended:
            first = uniform(trial, b'twobit-a', ct_size * 8)
            second = uniform(trial, b'twobit-b', ct_size * 8 - 1)
            if second >= first:
                second += 1
            mutations += [('twobit', [(first // 8, 1 << (first % 8)),
                                     (second // 8, 1 << (second % 8))]), ('random', [])]
        for kind, edits in mutations:
            altered = buffer(draw(trial, b'random-ct', ct_size) if kind == 'random' else bytes(ct))
            for offset, change in edits:
                altered[offset] ^= change
            assert dec(actual, altered, sk) == 0
            reject(fallback, buffer(bytes(sk)[-32:]), altered)
            same = bytes(actual) == bytes(expected)
            nonreject = bytes(actual) != bytes(fallback)
            counts[kind + '_same'] += same
            counts[kind + '_nonreject'] += nonreject
            if kind == 'bit':
                region = 'v' if bit // 8 >= ct_size - v_bytes else 'u'
                regions[region]['trials'] += 1
                regions[region]['nonreject'] += nonreject
            if nonreject and sum(example['kind'] == kind for example in examples) < 10:
                examples.append(dict(trial=trial, kind=kind, edits=edits, same_secret=same))
    report = dict(variant=args.variant, precision=args.precision, sessions=args.sessions,
                  seed=args.seed, coins_generator='SHAKE256 from Python hashlib; deterministic test inputs',
                  counts=counts, wilson95={key: interval(value, args.sessions) for key, value in counts.items()},
                  extended=args.extended, bit_regions=regions, first_failures=examples, library_sha256=hashlib.sha256(args.library.read_bytes()).hexdigest())
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('variant', 'precision', 'sessions', 'counts')}), flush=True)


if __name__ == '__main__':
    main()
