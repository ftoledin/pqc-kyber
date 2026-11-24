#!/usr/bin/env python3
"""Run five disjoint SHAKE256 test streams for each Q32 Kyber variant."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import date
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from estimate_errors import interval


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library-dir', type=Path, required=True,
                        help='Contains libkyber512_q32.so, libkyber768_q32.so, libkyber1024_q32.so')
    parser.add_argument('--sessions-per-seed', type=int, default=20000)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.sessions_per_seed <= 0:
        parser.error('--sessions-per-seed must be positive')
    script = Path(__file__).with_name('estimate_errors.py')
    with tempfile.TemporaryDirectory(prefix='kyber-sampling-') as directory:
        def run_variant(variant):
            reports = []
            for batch in range(1, 6):
                target = Path(directory) / f'{variant}-{batch}.json'
                command = [sys.executable, str(script), '--library',
                           str(args.library_dir.resolve() / f'libkyber{variant}_q32.so'),
                           '--variant', str(variant), '--precision', '32', '--extended',
                           '--sessions', str(args.sessions_per_seed),
                           '--seed', f'q32-extended-v1-seed-{batch}', '--output', str(target)]
                subprocess.run(command, check=True, capture_output=True, text=True)
                r = json.loads(target.read_text()); reports.append(r)
                print(f"Kyber{variant} seed {batch}/5: {r['counts']}", flush=True)
            return reports
        reports = []
        with ThreadPoolExecutor(max_workers=3) as pool:
            futures = [pool.submit(run_variant, variant) for variant in (512, 768, 1024)]
            for future in as_completed(futures):
                reports.extend(future.result())
        aggregate = []
        for variant in (512, 768, 1024):
            batches = [r for r in reports if r['variant'] == variant]
            total = sum(r['sessions'] for r in batches)
            counts = {key: sum(r['counts'][key] for r in batches) for key in batches[0]['counts']}
            regions = {name: {key: sum(r['bit_regions'][name][key] for r in batches)
                              for key in ('trials', 'nonreject')} for name in ('u', 'v')}
            aggregate.append(dict(variant=variant, sessions=total, counts=counts,
                                  bit_regions=regions,
                                  wilson95={key: interval(value, total) for key, value in counts.items()}))
        result = dict(date=str(date.today()), precision=32, seeds=5,
                      sessions_per_seed=args.sessions_per_seed,
                      core_sha256=hashlib.sha256(Path('hash_options/chaotic_hash.c').read_bytes()).hexdigest(),
                      model='Uniform independent test coins; bit, byte XOR 1..255, two distinct bits, full random ciphertext',
                      aggregate=aggregate, batches=sorted(reports, key=lambda r: (r['variant'], r['seed'])))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + '\n')
        print('Campaign complete: ' + str(args.output), flush=True)


if __name__ == '__main__':
    main()
