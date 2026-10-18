#!/usr/bin/env python3
"""Compare the production C++ evaluator with a saved production-Python oracle.

Usage: test-cartographer-reference.py CPP_TEST_EXECUTABLE reference.json
The oracle must come from pinned Cartographer3D production classes, not this file.
No third-party Python dependencies are needed to consume the saved oracle.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


def main():
    executable, source = sys.argv[1:]
    oracle = json.loads(Path(source).read_text())
    parameters = oracle['model']
    profile = json.loads((Path(__file__).resolve().parents[1] / 'src/Modules/CartographerV3/profiles.json').read_text())['profiles'][0]
    errors = []
    frequency_errors = []
    with tempfile.TemporaryDirectory(prefix='cartographer-oracle-') as temporary:
        for thermal, key in [(False, 'raw_cases'), (True, 'temperature_cases')]:
            b = bytearray(320)
            b[:8] = b'CARTOV3\0'
            struct.pack_into('<II', b, 8, 1, 320)
            b[16:22] = bytes.fromhex(oracle['identity']['uuid'])
            b[24:56] = bytes.fromhex(profile['identify_sha256'])
            surface = oracle['identity']['surface'].encode('ascii')
            b[56:56+len(surface)] = surface
            struct.pack_into('<III', b, 88, int(thermal), 1001, 1001)
            struct.pack_into('<2d', b, 104, *parameters['domain'])
            struct.pack_into('<10d', b, 120, *parameters['coefficients'])
            heights = [item['height_mm'] for item in oracle['raw_cases']]
            struct.pack_into('<8d', b, 200, parameters['reference_temperature'], 20 if thermal else 24.5,
                             60 if thermal else 25.5, parameters['z_offset'], min(heights), max(heights), 0, 0)
            if thermal:
                struct.pack_into('<5d', b, 264, *parameters['temperature_parameters'])
            struct.pack_into('<I', b, 316, zlib.crc32(b[:316]))
            model = Path(temporary) / 'oracle.cart'
            model.write_bytes(b)
            cases = oracle[key] + [item for item in oracle['out_of_domain_cases'] if item['temperature_compensation'] == thermal]
            inputs = ''.join(f"{item['frequency_hz']:.17g},{item['temperature_c']:.17g}\n" for item in cases)
            result = subprocess.run([executable, '--batch', str(model)], input=inputs, text=True, capture_output=True, check=True)
            outputs = result.stdout.splitlines()
            assert len(outputs) == len(cases)
            for case, line in zip(cases, outputs):
                status, height, frequency = line.split(',')
                if case['status'] != 'valid':
                    assert status == 'domain', (case, line)
                    continue
                assert status == 'none', (case, line)
                errors.append(abs(float(height) - case['height_mm']))
                frequency_errors.append(abs(float(frequency) - case['compensated_frequency_hz']))
    assert len(errors) == oracle['counts']['valid_total']
    assert max(errors) <= 0.001, max(errors)
    print(json.dumps({'origin': 'synthetic; actual upstream Python versus production C++',
                      'valid_cases': len(errors), 'out_of_domain_cases': len(oracle['out_of_domain_cases']),
                      'max_height_error_mm': max(errors), 'max_frequency_error_hz': max(frequency_errors),
                      'oracle_sha256': hashlib.sha256(Path(source).read_bytes()).hexdigest()}, indent=2))


if __name__ == '__main__':
    main()
