#!/usr/bin/env python3
"""Check the demo AES-GCM framing against Python cryptography's standard API.

The C++ audit executable exports ephemeral test secrets through a pipe; this
script does not save keys or secrets. Requires the Python cryptography package.
"""
import argparse
import json
from pathlib import Path
import subprocess

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers.aead import AESGCM


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    fixtures = json.loads(subprocess.check_output([str(args.executable.resolve()), '--interop'], text=True))
    checks = 0
    for fixture in fixtures:
        message, frame, secret = (bytes.fromhex(fixture[k]) for k in ('message', 'frame', 'test_secret'))
        prefix = fixture['prefix_size']
        nonce = frame[5 + fixture['kem_size']:5 + fixture['kem_size'] + 12]
        cipher = AESGCM(secret)
        assert cipher.decrypt(nonce, frame[prefix:], frame[:prefix]) == message
        assert cipher.encrypt(nonce, message, frame[:prefix]) == frame[prefix:]
        checks += 2
        for payload, aad in [(frame[prefix:-1] + bytes([frame[-1] ^ 1]), frame[:prefix]),
                             (frame[prefix:], frame[:prefix - 1] + bytes([frame[prefix - 1] ^ 1]))]:
            try:
                cipher.decrypt(nonce, payload, aad)
            except InvalidTag:
                checks += 1
            else:
                raise AssertionError('Mutated tag/AAD accepted')
    report = dict(fixtures=len(fixtures), checks=checks, failures=0,
                  message_lengths=[len(bytes.fromhex(f['message'])) for f in fixtures],
                  implementation='Python cryptography AESGCM API vs C++ OpenSSL EVP demo frame')
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
