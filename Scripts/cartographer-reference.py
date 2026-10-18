#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate synthetic height goldens with pinned Cartographer3D production code.

The upstream models and license are from Cartographer3D/cartographer3d-plugin,
commit e94dcfc98644dee08452e36338cafa2eb41753db (GNU GPLv3). This developer
verification helper imports that preserved implementation; it does not install
dependencies, update repositories, communicate with hardware or move a printer.
NumPy and typing_extensions must already be available in the selected Python.

Example:
  python3 Scripts/cartographer-reference.py --plugin-root /path/to/cartographer3d-plugin --output /path/to/reference.json
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

PINNED_COMMIT = 'e94dcfc98644dee08452e36338cafa2eb41753db'
PINNED_SOURCE_TREE_SHA256 = '524e824d135983ee544a53685d8bbc8efc095c673c5c01bdb9cc0feca992b925'
COEFFICIENTS = [2.75, 2.5, 0.06, 0.02, -0.008, 0.005, 0.003, -0.002, 0.001, 0.0005]
DOMAIN = [1 / 4_000_000, 1 / 3_000_000]
Z_OFFSET = -0.12
REFERENCE_TEMPERATURE = 25.0
TEMPERATURE_PARAMETERS = [5.367848131964879e-06, 1.7559290784042338,
                          0.0007546669217813525, -343.290029518937,
                          32917500 * 24_000_000 / (2 ** 28)]


def verify_source(plugin):
    """Verify every Python source, including imports, before executing it.

    Exact content hashes also permit an exported source tree without .git.
    HEAD alone is insufficient because a pinned checkout may have local edits.
    """
    source_root = plugin / 'src/cartographer'
    files = sorted(source_root.rglob('*.py')) + [plugin / 'LICENSE']
    if not source_root.is_dir() or not files:
        raise ValueError('plugin root must contain src/cartographer and LICENSE')
    digest = hashlib.sha256()
    for source in files:
        digest.update(source.relative_to(plugin).as_posix().encode() + b'\0')
        digest.update(source.read_bytes() + b'\0')
    actual = digest.hexdigest()
    if actual != PINNED_SOURCE_TREE_SHA256:
        raise ValueError('plugin sources differ from the pinned Cartographer3D revision')
    try:
        git_root = subprocess.check_output(['git', '-C', str(plugin), 'rev-parse', '--show-toplevel'],
                                           text=True, stderr=subprocess.DEVNULL).strip()
        commit = (subprocess.check_output(['git', '-C', str(plugin), 'rev-parse', 'HEAD'],
                                          text=True, stderr=subprocess.DEVNULL).strip()
                  if Path(git_root).resolve() == plugin else None)
    except (OSError, subprocess.CalledProcessError):
        commit = None
    return actual, commit


def generate(plugin):
    tree_sha256, checkout_commit = verify_source(plugin)
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(plugin / 'src'))
    import numpy as np
    from cartographer.coil.temperature_compensation import CoilTemperatureCompensationModel
    from cartographer.interfaces.configuration import CoilCalibrationConfiguration, ScanModelConfiguration
    from cartographer.interfaces.printer import CoilCalibrationReference
    from cartographer.probe.scan_model import ScanModel

    class ReferenceMcu:
        def get_coil_reference(self):
            return CoilCalibrationReference(TEMPERATURE_PARAMETERS[4], 20.334694110249814)

    config = ScanModelConfiguration('synthetic-oracle', COEFFICIENTS, tuple(DOMAIN),
                                    Z_OFFSET, REFERENCE_TEMPERATURE)
    raw_model = ScanModel(config, None)
    temperature_model = CoilTemperatureCompensationModel(
        CoilCalibrationConfiguration(*TEMPERATURE_PARAMETERS[:4]), ReferenceMcu())
    compensated_model = ScanModel(config, temperature_model)

    def case(frequency, temperature, compensate):
        model = compensated_model if compensate else raw_model
        value = model.frequency_to_distance(frequency, temperature=temperature)
        corrected = (temperature_model.compensate(frequency, temperature, REFERENCE_TEMPERATURE)
                     if compensate else frequency)
        result = {
            'frequency_hz': float(frequency),
            'temperature_c': float(temperature),
            'temperature_compensation': bool(compensate),
            'compensated_frequency_hz': float(corrected),
            'inverse_compensated_frequency': float(1 / corrected),
            'height_mm': float(value) if math.isfinite(value) else None,
            'status': 'valid' if math.isfinite(value) else 'out_of_domain',
        }
        if not math.isfinite(value):
            result['upstream_result'] = '+inf' if value > 0 else '-inf'
        return result

    # Independent evaluation points, not fitting observations. Include endpoints.
    frequencies = [1 / (DOMAIN[0] + (DOMAIN[1] - DOMAIN[0]) * i / 1000)
                   for i in range(1001)]
    raw = [case(frequency, REFERENCE_TEMPERATURE, False) for frequency in frequencies]
    temperature, outside = [], []
    for source_temperature in (20.0, 25.0, 40.0, 60.0):
        for frequency in frequencies:
            item = case(frequency, source_temperature, True)
            (temperature if item['status'] == 'valid' else outside).append(item)
    for frequency in (2_990_000.0, 4_010_000.0):
        outside.append(case(frequency, REFERENCE_TEMPERATURE, False))
    if len(raw) != 1001 or len(temperature) != 3999 or len(outside) != 7:
        raise ValueError('production oracle produced unexpected case counts')
    for item in raw + temperature:
        if item['status'] != 'valid' or not DOMAIN[0] <= item['inverse_compensated_frequency'] <= DOMAIN[1]:
            raise ValueError('production oracle produced an invalid in-domain sample')

    derivative_lower = COEFFICIENTS[1] - sum(k * abs(COEFFICIENTS[k]) for k in range(2, 10))
    source_paths = ['src/cartographer/probe/scan_model.py',
                    'src/cartographer/coil/temperature_compensation.py',
                    'src/cartographer/interfaces/configuration.py', 'LICENSE']
    return {
        'format': 'cartographer-height-upstream-oracle-v1',
        'origin': 'synthetic; production Python evaluation; no physical measurements',
        'provenance': {
            'repository': 'https://github.com/Cartographer3D/cartographer3d-plugin',
            'commit': PINNED_COMMIT,
            'checkout_commit': checkout_commit,
            'verification': 'exact complete Python source tree and LICENSE SHA256',
            'source_tree_sha256': tree_sha256,
            'source_tree_hash_order': 'sorted src/cartographer/**/*.py, then LICENSE; path NUL content NUL',
            'license': 'GPL-3.0',
            'numpy_version': np.__version__,
            'source_sha256': {path: hashlib.sha256((plugin / path).read_bytes()).hexdigest()
                              for path in source_paths},
            'temperature_parameters_source': 'Cartographer3D/docs: cartographer-probe/fine-tuning/temperature-differential-calibration-beta.md',
            'temperature_reference_origin': 'stock MCU base count; not per-device factory calibration',
        },
        'identity': {
            'uuid': '010203040506',
            'uuid_origin': 'synthetic fixture, not a real device',
            'profile': 'CARTOGRAPHER V3 6.1.0',
            'surface': 'synthetic-oracle-surface',
        },
        'model': {
            'basis': 'power',
            'window': [-1.0, 1.0],
            'coefficients': COEFFICIENTS,
            'domain': DOMAIN,
            'z_offset': Z_OFFSET,
            'reference_temperature': REFERENCE_TEMPERATURE,
            'temperature_parameters_order': ['a_a', 'a_b', 'b_a', 'b_b', 'fmin'],
            'temperature_parameters': TEMPERATURE_PARAMETERS,
        },
        'monotonicity_proof': {
            'normalized_derivative_lower_bound': derivative_lower,
            'method': 'c1 - sum(k*abs(ck), k=2..9) on [-1,1]',
            'height_increases_with_inverse_frequency': True,
        },
        'endpoints': [raw[0], raw[-1]],
        'raw_cases': raw,
        'temperature_cases': temperature,
        'out_of_domain_cases': outside,
        'counts': {
            'raw_valid': len(raw),
            'temperature_valid': len(temperature),
            'valid_total': len(raw) + len(temperature),
            'out_of_domain': len(outside),
            'temperature_valid_by_c': {str(t): sum(c['temperature_c'] == t for c in temperature)
                                       for t in (20.0, 25.0, 40.0, 60.0)},
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plugin-root', type=Path, required=True,
                        help='preserved Cartographer3D plugin checkout or exported source tree')
    parser.add_argument('--output', type=Path, required=True, help='destination JSON oracle')
    args = parser.parse_args()
    try:
        data = generate(args.plugin_root.resolve())
        rendered = json.dumps(data, ensure_ascii=True, allow_nan=False, indent=2) + '\n'
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding='utf-8')
    except (OSError, ValueError, ImportError) as error:
        parser.exit(1, f'Cannot generate Cartographer reference: {error}\n')
    print(json.dumps({'output': str(args.output), **data['counts']}, indent=2))


if __name__ == '__main__':
    main()
