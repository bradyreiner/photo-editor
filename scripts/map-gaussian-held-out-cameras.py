#!/usr/bin/env python3
"""Map independent held-out cameras into an accepted reconstruction's gauge.

Evaluation only: the similarity was fitted to training-camera centers. No
held-out pose or pixel is fitted here, and these files must not enter training.
"""
import argparse
import json
import math
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

_support = runpy.run_path(str(Path(__file__).with_name('compare-gaussian-photo-alignment.py')))
digest, read_json = _support['digest'], _support['read_json']
transpose, multiply, vector = _support['transpose'], _support['multiply'], _support['vector']
reference_cameras = _support['reference_cameras']


def map_camera(camera, gauge):
    scale, rotation, translation = gauge['scale'], gauge['rotation'], gauge['translation']
    if not isinstance(scale, (int, float)) or isinstance(scale, bool) or not math.isfinite(scale) or scale <= 0 or \
            len(rotation) != 3 or any(len(row) != 3 for row in rotation) or len(translation) != 3:
        raise ValueError('Invalid training-camera gauge')
    if not all(math.isfinite(v) for v in [*translation, *(x for row in rotation for x in row)]):
        raise ValueError('Non-finite training-camera gauge')
    gram = multiply(transpose(rotation), rotation)
    determinant = sum(rotation[0][j]*(rotation[1][(j+1)%3]*rotation[2][(j+2)%3]-rotation[1][(j+2)%3]*rotation[2][(j+1)%3]) for j in range(3))
    if max(abs(gram[i][j]-(i == j)) for i in range(3) for j in range(3)) > 1e-8 or abs(determinant-1) > 1e-8:
        raise ValueError('Gauge must use a proper orthonormal rotation')
    inverse = transpose(rotation)
    position = [x/scale for x in vector(inverse, [x-y for x, y in zip(camera['position'], translation)])]
    return {**camera, 'position': position, 'rotation': multiply(inverse, camera['rotation'])}


def self_test():
    camera = {'id': 1, 'img_name': 'held-out', 'position': [-1, -2, 11],
              'rotation': [[0, -1, 0], [1, 0, 0], [0, 0, 1]], 'fx': 900, 'fy': 901, 'width': 1000, 'height': 800}
    gauge = {'scale': 2, 'rotation': [[0, -1, 0], [1, 0, 0], [0, 0, 1]], 'translation': [3, -4, 5]}
    mapped = map_camera(camera, gauge)
    assert mapped['position'] == [1, 2, 3]
    assert mapped['rotation'] == [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
    assert all(mapped[k] == camera[k] for k in ('fx', 'fy', 'width', 'height', 'img_name'))
    for bad in ({**gauge, 'scale': 0}, {**gauge, 'rotation': [[-1, 0, 0], [0, 1, 0], [0, 0, 1]]}):
        try:
            map_camera(camera, bad)
            raise AssertionError('Invalid evaluation gauge accepted')
        except ValueError:
            pass
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        source = root/'held-out.png'; source.write_bytes(b'held-out source identity fixture')
        calibration = root/'reference.json'; calibration.write_text(json.dumps([camera]))
        sparse = root/'sparse'; sparse.mkdir()
        artifact = sparse/'images.bin'; artifact.write_bytes(b'audited model identity fixture')
        plan = {'evaluationCalibration': {'path': str(calibration), 'sha256': digest(calibration)},
                'images': [{'source': str(source), 'sourceSha256': digest(source), 'heldOut': True}],
                'heldOutImageCount': 1, 'trainingImageCount': 3}
        for i in range(3):
            training_source = root/f'training-{i}.png'; training_source.write_bytes(b'training source identity fixture')
            plan['images'].append({'source': str(training_source), 'sourceSha256': digest(training_source), 'heldOut': False})
        plan_path = root/'plan.json'; plan_path.write_text(json.dumps(plan))
        audit = {'schema': 'looksy-photo-capture-quality-audit-v1', 'alignmentQualityPassed': True,
                 'gates': {'syntheticFixtureOnly': True}, 'planSha256': digest(plan_path),
                 'candidate': {'selectedModel': str(sparse), 'sparseFileSha256': {'images.bin': digest(artifact)},
                               'similarityEstimatedToReference': gauge}}
        audit_path = root/'audit.json'; audit_path.write_text(json.dumps(audit))
        command = [sys.executable, str(Path(__file__).resolve()), '--audit', str(audit_path), '--plan', str(plan_path), '--output']
        result = subprocess.run([*command, str(root/'mapped')], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        assert read_json(root/'mapped/cameras.json')[0] == mapped
        artifact.write_bytes(b'changed after audit')
        result = subprocess.run([*command, str(root/'rejected')], capture_output=True, text=True)
        assert result.returncode != 0 and not (root/'rejected').exists()
    print('Held-out camera mapping: independent inverse rotation/scale/translation, unchanged optics and invalid-gauge rejection pass')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--audit', type=Path)
    parser.add_argument('--plan', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not all((args.audit, args.plan, args.output)) or not args.output.is_absolute() or args.output.exists():
        parser.error('Provide audit, plan and a new absolute output directory')
    audit, plan = read_json(args.audit), read_json(args.plan)
    if audit.get('schema') != 'looksy-photo-capture-quality-audit-v1' or audit.get('alignmentQualityPassed') is not True or \
            not audit.get('gates') or not all(v is True for v in audit['gates'].values()) or digest(args.plan) != audit['planSha256']:
        raise ValueError('An accepted camera-quality audit and its unchanged plan are required')
    calibration = Path(plan['evaluationCalibration']['path'])
    if digest(calibration) != plan['evaluationCalibration']['sha256']:
        raise ValueError('Reference calibration changed')
    references = reference_cameras(calibration)
    rows = plan['images']
    training, held_out = [{Path(row['source']).stem for row in rows if row['heldOut'] == value} for value in (False, True)]
    if len(training) < 3 or training & held_out or len(held_out) != plan['heldOutImageCount'] or len(training) != plan['trainingImageCount']:
        raise ValueError('Invalid or overlapping training/held-out names')
    for row in rows:
        if digest(Path(row['source'])) != row['sourceSha256']:
            raise ValueError('A source photo changed')
    candidate = audit['candidate']
    model = Path(candidate['selectedModel'])
    for name, expected in candidate['sparseFileSha256'].items():
        if Path(name).name != name or digest(model/name) != expected:
            raise ValueError('The audited sparse reconstruction changed')
    mapped = [map_camera(references[name], candidate['similarityEstimatedToReference']) for name in sorted(held_out)]
    args.output.mkdir()
    (args.output/'cameras.json').write_text(json.dumps(mapped, indent=2)+'\n')
    receipt = {'schema': 'looksy-held-out-camera-mapping-v1', 'auditSha256': digest(args.audit),
               'planSha256': digest(args.plan), 'referenceCalibrationSha256': digest(calibration),
               'outputCameraSha256': digest(args.output/'cameras.json'), 'frameCount': len(mapped),
               'poseFitUsesTrainingCamerasOnly': True, 'heldOutPoseOrPixelFitting': False,
               'scope': 'Evaluation cameras only; no resampling, optical changes or held-out pose refinement; do not supply these files to training'}
    (args.output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
