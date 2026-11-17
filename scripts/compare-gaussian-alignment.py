#!/usr/bin/env python3
"""Compare two frozen video-frame alignment experiments without training claims.

Reads production scene receipts and COLMAP binary models. Interval coverage is
comparable when both inputs have the same source, duration and interval count;
reprojection errors and recovered poses remain separate alignment domains.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import struct


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def read_json(path, maximum=4 * 1024 * 1024):
    if path.is_symlink() or not path.is_file() or path.stat().st_size > maximum:
        raise ValueError(f'Invalid or oversized receipt: {path}')
    return json.loads(path.read_text())


class Binary:
    def __init__(self, path):
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 1024**3:
            raise ValueError(f'Invalid COLMAP file: {path}')
        self.path = path
        self.size = path.stat().st_size
        self.file = path.open('rb')

    def unpack(self, pattern):
        size = struct.calcsize('<' + pattern)
        value = self.file.read(size)
        if len(value) != size:
            raise ValueError(f'Truncated COLMAP file: {self.path}')
        return struct.unpack('<' + pattern, value)

    def skip(self, count):
        if count < 0 or self.file.tell() + count > self.size:
            raise ValueError(f'Invalid COLMAP track length: {self.path}')
        self.file.seek(count, 1)

    def name(self):
        value = bytearray()
        for _ in range(1024):
            byte = self.file.read(1)
            if byte == b'\0':
                return value.decode('utf8')
            if not byte:
                break
            value.extend(byte)
        raise ValueError(f'Invalid COLMAP image name: {self.path}')

    def finish(self):
        if self.file.tell() != self.size:
            raise ValueError(f'Unexpected COLMAP trailing bytes: {self.path}')
        self.file.close()


def images(path, frame_count):
    source = Binary(path)
    count, = source.unpack('Q')
    if count > frame_count:
        raise ValueError('Model contains more images than the frozen input')
    result = []
    for _ in range(count):
        fields = source.unpack('I7dI')
        name = source.name()
        match = re.fullmatch(r'frame_(\d{6})\.png', name)
        if not match or int(match[1]) >= frame_count:
            raise ValueError('Registered image is outside the canonical interval set')
        if not all(math.isfinite(value) for value in fields[1:8]) or abs(sum(value*value for value in fields[1:5]) - 1) > 1e-4:
            raise ValueError('Invalid camera pose')
        points, = source.unpack('Q')
        if points > 1_000_000:
            raise ValueError('Oversized image observations')
        source.skip(points * 24)
        result.append({'name': name, 'interval': int(match[1]), 'imageId': fields[0], 'cameraId': fields[-1]})
    source.finish()
    if len({value['interval'] for value in result}) != count or len({value['imageId'] for value in result}) != count:
        raise ValueError('Duplicate registered images')
    return sorted(result, key=lambda value: value['interval'])


def camera(path):
    source = Binary(path)
    count, = source.unpack('Q')
    if count != 1:
        raise ValueError('The frozen video experiment requires one shared camera')
    camera_id, model, width, height = source.unpack('IIQQ')
    if model != 2 or not (16 <= width <= 4096 and 16 <= height <= 4096):
        raise ValueError('Expected the production SIMPLE_RADIAL video camera')
    focal, cx, cy, radial = source.unpack('4d')
    source.finish()
    if not all(math.isfinite(value) for value in (focal, cx, cy, radial)) or focal <= 0:
        raise ValueError('Invalid estimated camera parameters')
    return {'id': camera_id, 'model': 'SIMPLE_RADIAL', 'width': width, 'height': height,
            'focalPixels': focal, 'cx': cx, 'cy': cy, 'radialK1': radial}


def point_errors(path, frame_count):
    source = Binary(path)
    count, = source.unpack('Q')
    if count > 2_000_000:
        raise ValueError('Oversized sparse point set')
    errors = []
    observations = 0
    for _ in range(count):
        value = source.unpack('Q3d3BdQ')
        error, tracks = value[-2:]
        if not math.isfinite(error) or error < 0 or tracks > frame_count:
            raise ValueError('Invalid sparse point error/track')
        source.skip(tracks * 8)
        errors.append(error)
        observations += tracks
    source.finish()
    return {'count': count, 'observations': observations,
            'meanReprojectionErrorPixels': sum(errors)/count if count else None,
            'scope': 'Each model has its own estimated poses and point set; errors are not common-pose photographic quality scores'}


def longest_missing(mask):
    longest = current = 0
    for connected in mask:
        current = 0 if connected else current + 1
        longest = max(longest, current)
    return longest


def analyze(project, frozen):
    state = read_json(project / 'scene.json')
    video = state.get('video', {})
    count = frozen['frameCount']
    if state.get('sourceCount') != count or video.get('sourceSha256') != frozen['sourceSha256']:
        raise ValueError('Project/source does not match the frozen input')
    if state.get('matchingStrategy') != 'video-sequential-guided-20' or state.get('cameraGroupCount') != 0:
        raise ValueError('Project did not use the frozen production video path')
    capture = state.get('capture', {}).get('frames', [])
    if len(capture) != count:
        raise ValueError('Canonical preparation is incomplete')
    for frame in capture:
        path = Path(frame['sourcePath'])
        expected = frozen['frames'].get(str(path))
        if not expected or frame.get('sourceSha256') != expected or digest(path) != expected:
            raise ValueError('A frozen source frame changed')
    models = []
    for folder in sorted((project / 'sparse').iterdir()):
        if folder.is_symlink() or not folder.is_dir() or not (folder / 'images.bin').exists():
            continue
        registered = images(folder / 'images.bin', count)
        if registered:
            models.append({'folder': folder.name, 'registered': registered,
                           'camera': camera(folder / 'cameras.bin'), 'points': point_errors(folder / 'points3D.bin', count)})
    if not models:
        raise ValueError('No finished alignment component')
    largest = max(models, key=lambda value: len(value['registered']))
    connected = {value['interval'] for value in largest['registered']}
    if state.get('registeredViews') != len(connected):
        raise ValueError('Controller component count differs from the parsed model')
    if any(value['cameraId'] != largest['camera']['id'] for value in largest['registered']):
        raise ValueError('Image camera references do not match the shared calibration')
    mask = [index in connected for index in range(count)]
    return {'project': str(project), 'sceneReceiptSha256': digest(project / 'scene.json'),
            'sourceSha256': video['sourceSha256'], 'durationSeconds': video['durationSeconds'],
            'frameCount': count, 'componentCounts': [len(model['registered']) for model in models],
            'largestComponent': largest['folder'], 'registeredViews': len(connected),
            'registeredFraction': len(connected)/count,
            'firstRegisteredInterval': min(connected), 'lastRegisteredInterval': max(connected),
            'registeredIntervalSpan': max(connected)-min(connected), 'longestMissingIntervalRun': longest_missing(mask),
            'connectedMask': mask, 'estimatedCamera': largest['camera'], 'sparsePoints': largest['points'],
            'state': state['state'], 'trainingStarted': 'trainingSchedule' in state,
            'sparseFileSha256': {name: digest(project / 'sparse' / largest['folder'] / name)
                                 for name in ('images.bin', 'cameras.bin', 'points3D.bin')}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    plan = read_json(args.plan)
    root = args.plan.resolve().parent
    if not args.output.is_absolute() or args.output.exists():
        raise ValueError('Output must be a new absolute directory')
    results = {mode: analyze(root / mode, plan['inputs'][mode]) for mode in ('uniform', 'sharp')}
    baseline, candidate = results['uniform'], results['sharp']
    if baseline['sourceSha256'] != candidate['sourceSha256'] or baseline['frameCount'] != candidate['frameCount'] or baseline['durationSeconds'] != candidate['durationSeconds']:
        raise ValueError('Comparators are not the same source/interval budget')
    if baseline['trainingStarted'] or candidate['trainingStarted']:
        raise ValueError('Alignment-only experiment unexpectedly started training')
    gain = candidate['registeredViews'] - baseline['registeredViews']
    gates = {'connectedViewGain': gain >= plan['hypothesisGate']['minimumConnectedViewGain'],
             'intervalSpanDoesNotShrink': candidate['registeredIntervalSpan'] >= baseline['registeredIntervalSpan'],
             'missingRunDoesNotIncrease': candidate['longestMissingIntervalRun'] <= baseline['longestMissingIntervalRun']}
    report = {'schema': 'looksy-gaussian-alignment-comparison-v1', 'planSha256': digest(args.plan),
              'results': results, 'connectedViewGain': gain, 'hypothesisGates': gates,
              'captureCoverageGates': {name: value['registeredViews'] >= plan['qualityCoverageGate']['minimumConnectedViews']
                                       for name, value in results.items()},
              'alignmentHypothesisPassed': all(gates.values()), 'trainedQualityImprovementQualified': False,
              'wholeVideoParityQualified': False,
              'scope': 'One paired production alignment observation; calibrations differ. No common-pose photographic metric, training result or default promotion.'}
    args.output.mkdir()
    (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    with (args.output / 'intervals.csv').open('w', newline='') as target:
        writer = csv.writer(target)
        writer.writerow(['interval', 'nominalIntervalStartSeconds', 'uniformConnected', 'selectedConnected'])
        for index in range(baseline['frameCount']):
            writer.writerow([index, index*baseline['durationSeconds']/baseline['frameCount'],
                             baseline['connectedMask'][index], candidate['connectedMask'][index]])
    print(json.dumps({'registered': {name: value['registeredViews'] for name, value in results.items()},
                      'connectedViewGain': gain, 'alignmentHypothesisPassed': report['alignmentHypothesisPassed']}))


if __name__ == '__main__':
    main()
