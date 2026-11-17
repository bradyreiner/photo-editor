#!/usr/bin/env python3
"""Evaluate a frozen photo-intrinsics experiment against independent camera poses.

Reference camera calibration is evaluation-only. Gauge fitting uses registered
training camera centers; no held-out image enters alignment or training.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import runpy
import sqlite3
import statistics
import tempfile
import struct

_support = runpy.run_path(str(Path(__file__).with_name('compare-gaussian-alignment.py')))
Binary = _support['Binary']
read_json = _support['read_json']
digest = _support['digest']


def transpose(a):
    return [list(row) for row in zip(*a)]


def multiply(a, b):
    return [[sum(x*y for x, y in zip(row, column)) for column in zip(*b)] for row in a]


def vector(a, v):
    return [sum(x*y for x, y in zip(row, v)) for row in a]


def quaternion_matrix(q):
    w, x, y, z = q
    return [[1-2*(y*y+z*z), 2*(x*y-w*z), 2*(x*z+w*y)],
            [2*(x*y+w*z), 1-2*(x*x+z*z), 2*(y*z-w*x)],
            [2*(x*z-w*y), 2*(y*z+w*x), 1-2*(x*x+y*y)]]


def largest_eigenvector(source):
    a = [row[:] for row in source]
    v = [[float(i == j) for j in range(4)] for i in range(4)]
    for _ in range(100):
        p, q = max(((i, j) for i in range(4) for j in range(i+1, 4)), key=lambda ij: abs(a[ij[0]][ij[1]]))
        if abs(a[p][q]) < 1e-14*max(1, max(abs(a[i][i]) for i in range(4))):
            break
        angle = 0.5*math.atan2(2*a[p][q], a[q][q]-a[p][p])
        c, s = math.cos(angle), math.sin(angle)
        app, aqq, apq = a[p][p], a[q][q], a[p][q]
        for k in range(4):
            if k not in (p, q):
                akp, akq = a[k][p], a[k][q]
                a[k][p] = a[p][k] = c*akp-s*akq
                a[k][q] = a[q][k] = s*akp+c*akq
        a[p][p] = c*c*app-2*c*s*apq+s*s*aqq
        a[q][q] = s*s*app+2*c*s*apq+c*c*aqq
        a[p][q] = a[q][p] = 0
        for k in range(4):
            vkp, vkq = v[k][p], v[k][q]
            v[k][p], v[k][q] = c*vkp-s*vkq, s*vkp+c*vkq
    order = sorted(range(4), key=lambda i: a[i][i], reverse=True)
    if a[order[0]][order[0]]-a[order[1]][order[1]] < 1e-10*max(1, abs(a[order[0]][order[0]])):
        raise ValueError('Camera-center gauge is underconstrained')
    result = [row[order[0]] for row in v]
    norm = math.sqrt(sum(x*x for x in result))
    return [x/norm for x in result]


def similarity(estimated, reference):
    if len(estimated) != len(reference) or len(estimated) < 3:
        raise ValueError('At least three corresponding camera centers are required')
    n = len(estimated)
    ma = [sum(p[j] for p in estimated)/n for j in range(3)]
    mb = [sum(p[j] for p in reference)/n for j in range(3)]
    aa = [[p[j]-ma[j] for j in range(3)] for p in estimated]
    bb = [[p[j]-mb[j] for j in range(3)] for p in reference]
    s = [[sum(a[i]*b[j] for a, b in zip(aa, bb)) for j in range(3)] for i in range(3)]
    xx, xy, xz = s[0]
    yx, yy, yz = s[1]
    zx, zy, zz = s[2]
    horn = [[xx+yy+zz, yz-zy, zx-xz, xy-yx],
            [yz-zy, xx-yy-zz, xy+yx, xz+zx],
            [zx-xz, xy+yx, -xx+yy-zz, yz+zy],
            [xy-yx, xz+zx, yz+zy, -xx-yy+zz]]
    rotation = quaternion_matrix(largest_eigenvector(horn))
    denominator = sum(sum(x*x for x in a) for a in aa)
    scale = sum(sum(x*y for x, y in zip(vector(rotation, a), b)) for a, b in zip(aa, bb))/denominator
    if not math.isfinite(scale) or scale <= 0:
        raise ValueError('Invalid proper-rotation similarity scale')
    translation = [mb[j]-scale*vector(rotation, ma)[j] for j in range(3)]
    return scale, rotation, translation


def camera_models(path, maximum):
    source = Binary(path)
    count, = source.unpack('Q')
    if count < 1 or count > maximum:
        raise ValueError('Invalid camera count')
    cameras = {}
    for _ in range(count):
        identifier, model, width, height = source.unpack('IIQQ')
        if identifier in cameras or model != 2 or min(width, height) < 16 or max(width, height) > 4096:
            raise ValueError('Expected bounded SIMPLE_RADIAL cameras')
        focal, cx, cy, k1 = source.unpack('4d')
        if not all(math.isfinite(x) for x in (focal, cx, cy, k1)) or focal <= 0:
            raise ValueError('Invalid camera calibration')
        cameras[identifier] = {'focal': focal, 'cx': cx, 'cy': cy, 'radialK1': k1, 'width': width, 'height': height}
    source.finish()
    return cameras


def image_poses(path, allowed):
    source = Binary(path)
    count, = source.unpack('Q')
    if count > len(allowed):
        raise ValueError('More registered images than training inputs')
    images = {}
    for _ in range(count):
        fields = source.unpack('I7dI')
        name = source.name()
        q, t = fields[1:5], fields[5:8]
        if name not in allowed or name in images or not all(math.isfinite(x) for x in (*q, *t)) or abs(sum(x*x for x in q)-1) > 1e-4:
            raise ValueError('Invalid, duplicate, or non-training camera pose')
        rotation = transpose(quaternion_matrix(q))
        images[name] = {'cameraId': fields[-1], 'position': [-x for x in vector(rotation, t)], 'rotation': rotation}
        points, = source.unpack('Q')
        if points > 1_000_000:
            raise ValueError('Oversized image observations')
        source.skip(points*24)
    source.finish()
    return images


def database_features(path):
    if path.is_symlink() or not path.is_file():
        raise ValueError('Invalid feature database')
    with sqlite3.connect(path.resolve().as_uri()+'?mode=ro', uri=True) as database:
        cameras, = database.execute('select count(*) from cameras').fetchone()
        rows = database.execute('select i.name,k.rows,k.cols,k.data,d.rows,d.cols,d.data from images i '
                                'join keypoints k on k.image_id=i.image_id join descriptors d on d.image_id=i.image_id order by i.name')
        features = {name: {'keypoints': kr, 'keypointColumns': kc, 'keypointSha256': hashlib.sha256(kb).hexdigest(),
                           'descriptorRows': dr, 'descriptorColumns': dc, 'descriptorSha256': hashlib.sha256(db).hexdigest()}
                    for name, kr, kc, kb, dr, dc, db in rows}
        verified, correspondences = database.execute('select count(*),coalesce(sum(rows),0) from two_view_geometries where rows>0').fetchone()
    return {'cameraCount': cameras, 'features': features, 'verifiedPairs': verified, 'verifiedCorrespondences': correspondences}


def summarize(values):
    return {'median': statistics.median(values), 'mean': statistics.mean(values), 'maximum': max(values)}


def comparison_gates(plan, baseline, candidate, focal_gain, prepared_exact, features_exact):
    g = plan['gates']
    gates = {'coverage': candidate['registeredViews'] >= max(g['minimumRegistered'], baseline['registeredViews']),
             'coverageOrCommonFocalGain': candidate['registeredViews']-baseline['registeredViews'] >= g['ifBaselineBelow29MinimumAdditionalRegistered'] if baseline['registeredViews'] < 29 else focal_gain >= g['ifBothAtLeast29MinimumMedianFocalErrorImprovement'],
             'candidateFocal': candidate['focalRelativeError']['median'] <= g['candidateMedianFocalRelativeErrorMaximum'],
             'candidateRotation': candidate['rotationErrorDegrees']['median'] <= g['candidateMedianRotationErrorDegreesMaximum'],
             'preparedBytesExact': prepared_exact}
    if plan.get('schema') == 'looksy-photo-feature-quality-plan-v1':
        # This separate, before-run protocol changes the feature extractor.
        # The original intrinsics protocol always requires exact features.
        if not plan.get('featureContract') or not all(k in g for k in (
                'candidateMaximumPositionErrorNormalizedByCameraRadius', 'candidateMaximumRotationErrorDegrees')):
            raise ValueError('Feature-quality plan must declare feature changes and worst-camera gates')
        gates['featuresChangedAsPlanned'] = not features_exact
        gates['candidateMaximumPosition'] = candidate['positionErrorNormalizedByCameraRadius']['maximum'] <= g['candidateMaximumPositionErrorNormalizedByCameraRadius']
        gates['candidateMaximumRotation'] = candidate['rotationErrorDegrees']['maximum'] <= g['candidateMaximumRotationErrorDegrees']
    elif plan.get('schema') in ('looksy-photo-intrinsics-experiment-v1', 'looksy-photo-planar-link-plan-v1'):
        gates['featuresExact'] = features_exact
        if plan['schema'] == 'looksy-photo-planar-link-plan-v1':
            gates['candidateMaximumPosition'] = candidate['positionErrorNormalizedByCameraRadius']['maximum'] <= g['candidateMaximumPositionErrorNormalizedByCameraRadius']
            gates['candidateMaximumRotation'] = candidate['rotationErrorDegrees']['maximum'] <= g['candidateMaximumRotationErrorDegrees']
    else:
        raise ValueError('Unsupported comparison protocol')
    return gates


def reference_cameras(path):
    references = {}
    for camera in read_json(path):
        name, rotation, position = camera['img_name'], camera['rotation'], camera['position']
        if name in references or not isinstance(name, str) or len(rotation) != 3 or any(len(row) != 3 for row in rotation) or len(position) != 3:
            raise ValueError('Invalid or duplicate reference camera')
        numbers = [*position, *(v for row in rotation for v in row), camera['fx'], camera['fy'], camera['width'], camera['height']]
        if not all(isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) for v in numbers):
            raise ValueError('Non-finite reference calibration')
        if min(camera[k] for k in ('fx', 'fy', 'width', 'height')) <= 0:
            raise ValueError('Invalid reference image geometry')
        gram = multiply(transpose(rotation), rotation)
        determinant = sum(rotation[0][j]*(rotation[1][(j+1)%3]*rotation[2][(j+2)%3]-rotation[1][(j+2)%3]*rotation[2][(j+1)%3]) for j in range(3))
        if max(abs(gram[i][j]-(i == j)) for i in range(3) for j in range(3)) > 1e-5 or abs(determinant-1) > 1e-5:
            raise ValueError('Reference pose is not a proper rotation')
        references[name] = camera
    return references


def verify_prepared_mapping(project, rows):
    expected = {r['canonicalName']: r for r in rows if not r['heldOut']}
    if len(expected) != sum(not r['heldOut'] for r in rows) or None in expected:
        raise ValueError('Duplicate or missing canonical training names')
    if {p.name for p in (project/'images').glob('*.png')} != set(expected):
        raise ValueError('Prepared images differ from the frozen training-only set')
    receipt = project/'scene.json'
    if receipt.exists():
        frames = read_json(receipt).get('capture', {}).get('frames', [])
        actual = {Path(f['preparedPath']).name: f for f in frames}
        if len(actual) != len(frames) or set(actual) != set(expected):
            raise ValueError('Production preparation does not cover the frozen training set')
        for name, row in expected.items():
            frame = actual[name]
            if frame['sourceSha256'] != row['sourceSha256'] or Path(frame['sourcePath']).stem != Path(row['source']).stem:
                raise ValueError('Prepared image/source mapping differs from the frozen plan')


def sparse_components(project, allowed):
    components = []
    # COLMAP's initial mapper writes numbered models; mapper --input_path
    # writes the continued model directly into the requested output folder.
    for directory in [project/'sparse', *sorted((project/'sparse').iterdir())]:
        if directory.is_dir() and not directory.is_symlink() and (directory/'images.bin').is_file():
            components.append((directory, image_poses(directory/'images.bin', allowed)))
    if not components:
        raise ValueError('No completed sparse model')
    if len(components) > 1 and any(path == project/'sparse' for path, _ in components):
        raise ValueError('Mixed direct and numbered sparse models; use one fresh reconstruction output')
    if not any(poses for _, poses in components):
        raise ValueError('No registered camera poses')
    return components


def evaluate(project, rows, reference):
    verify_prepared_mapping(project, rows)
    allowed = {r['canonicalName']: Path(r['source']).stem for r in rows if not r['heldOut']}
    components = sparse_components(project, allowed)
    model, poses = max(components, key=lambda item: len(item[1]))
    cameras = camera_models(model/'cameras.bin', len(allowed))
    if any(p['cameraId'] not in cameras for p in poses.values()):
        raise ValueError('A pose references missing calibration')
    names = sorted(poses)
    scale, rotation, translation = similarity([poses[n]['position'] for n in names], [reference[allowed[n]]['position'] for n in names])
    gt_mean = [statistics.mean(reference[allowed[n]]['position'][j] for n in names) for j in range(3)]
    gt_radius = math.sqrt(statistics.mean(sum((reference[allowed[n]]['position'][j]-gt_mean[j])**2 for j in range(3)) for n in names))
    errors = []
    for name in names:
        pose, ref = poses[name], reference[allowed[name]]
        camera = cameras[pose['cameraId']]
        expected_focal = (ref['fx']*camera['width']/ref['width']+ref['fy']*camera['height']/ref['height'])/2
        transformed = [scale*x+translation[j] for j, x in enumerate(vector(rotation, pose['position']))]
        position_error = math.sqrt(sum((x-y)**2 for x, y in zip(transformed, ref['position'])))
        delta = multiply(transpose(ref['rotation']), multiply(rotation, pose['rotation']))
        angle = math.degrees(math.acos(max(-1, min(1, (sum(delta[j][j] for j in range(3))-1)/2))))
        errors.append({'canonicalName': name, 'sourceName': allowed[name], 'focalRelativeError': abs(camera['focal']/expected_focal-1),
                       'focalPixels': camera['focal'], 'referenceFocalPixels': expected_focal,
                       'rotationErrorDegrees': angle, 'positionErrorNormalizedByCameraRadius': position_error/gt_radius})
    return {'project': str(project), 'selectedModel': str(model), 'componentSizes': sorted(len(p) for _, p in components),
            'componentModels': [{'model': str(path), 'registeredViews': len(p)} for path, p in components],
            'registeredViews': len(poses), 'unregisteredNames': sorted(set(allowed)-set(poses)),
            'cameraModels': cameras, 'focalRelativeError': summarize([e['focalRelativeError'] for e in errors]),
            'rotationErrorDegrees': summarize([e['rotationErrorDegrees'] for e in errors]),
            'positionErrorNormalizedByCameraRadius': summarize([e['positionErrorNormalizedByCameraRadius'] for e in errors]),
            'similarityEstimatedToReference': {'scale': scale, 'rotation': rotation, 'translation': translation},
            'images': errors, 'database': database_features(project/'features.db'),
            'sparseFileSha256': {p.name: digest(p) for p in model.glob('*.bin')}}


def capture_quality_gates(candidate, limits):
    return {'coverage': candidate['registeredViews'] >= limits['minimumRegistered'],
            'medianFocal': candidate['focalRelativeError']['median'] <= limits['medianFocalRelativeErrorMaximum'],
            'medianRotation': candidate['rotationErrorDegrees']['median'] <= limits['medianRotationDegreesMaximum'],
            'maximumRotation': candidate['rotationErrorDegrees']['maximum'] <= limits['maximumRotationDegreesMaximum'],
            'medianPosition': candidate['positionErrorNormalizedByCameraRadius']['median'] <= limits['medianPositionNormalizedMaximum'],
            'maximumPosition': candidate['positionErrorNormalizedByCameraRadius']['maximum'] <= limits['maximumPositionNormalizedMaximum']}


def self_test():
    base = {'registeredViews': 31, 'focalRelativeError': {'median': .1}, 'rotationErrorDegrees': {'median': 1, 'maximum': 2},
            'positionErrorNormalizedByCameraRadius': {'maximum': .05}}
    limits = {'minimumRegistered': 29, 'ifBaselineBelow29MinimumAdditionalRegistered': 4,
              'ifBothAtLeast29MinimumMedianFocalErrorImprovement': .25, 'candidateMedianFocalRelativeErrorMaximum': .05,
              'candidateMedianRotationErrorDegreesMaximum': 2, 'candidateMaximumPositionErrorNormalizedByCameraRadius': .1,
              'candidateMaximumRotationErrorDegrees': 3}
    candidate = {**base, 'focalRelativeError': {'median': .01}}
    plan = {'schema': 'looksy-photo-intrinsics-experiment-v1', 'gates': limits}
    assert not all(comparison_gates(plan, base, candidate, .9, True, False).values())
    assert all(comparison_gates(plan, base, candidate, .9, True, True).values())
    feature_plan = {**plan, 'schema': 'looksy-photo-feature-quality-plan-v1', 'featureContract': 'Explicit feature experiment'}
    assert all(comparison_gates(feature_plan, base, candidate, .9, True, False).values())
    assert not all(comparison_gates(feature_plan, base, {**candidate, 'positionErrorNormalizedByCameraRadius': {'maximum': .5}}, .9, True, False).values())
    planar_plan = {**plan, 'schema': 'looksy-photo-planar-link-plan-v1'}
    assert all(comparison_gates(planar_plan, base, candidate, .9, True, True).values())
    assert not all(comparison_gates(planar_plan, base, {**candidate, 'rotationErrorDegrees': {'median': 1, 'maximum': 4}}, .9, True, True).values())
    capture_limits = {'minimumRegistered': 90, 'medianFocalRelativeErrorMaximum': .05, 'medianRotationDegreesMaximum': 2,
                      'maximumRotationDegreesMaximum': 3, 'medianPositionNormalizedMaximum': .025, 'maximumPositionNormalizedMaximum': .1}
    dense = {**candidate, 'registeredViews': 96, 'positionErrorNormalizedByCameraRadius': {'median': .01, 'maximum': .05}}
    assert all(capture_quality_gates(dense, capture_limits).values())
    assert not all(capture_quality_gates({**dense, 'registeredViews': 89}, capture_limits).values())
    assert not all(capture_quality_gates({**dense, 'positionErrorNormalizedByCameraRadius': {'median': .01, 'maximum': .11}}, capture_limits).values())
    # Independent exact 90-degree Z rotation, scale and translation.
    a = [[0, 0, 0], [1, 0, 0], [0, 2, 0], [0, 0, 3], [2, -1, 1]]
    b = [[3-2*p[1], -4+2*p[0], 5+2*p[2]] for p in a]
    scale, rotation, translation = similarity(a, b)
    assert abs(scale-2) < 1e-12
    assert max(abs(x-y) for x, y in zip(translation, [3, -4, 5])) < 1e-12
    for p, target in zip(a, b):
        actual = [scale*x+translation[j] for j, x in enumerate(vector(rotation, p))]
        assert max(abs(x-y) for x, y in zip(actual, target)) < 1e-12
    try:
        similarity([[0, 0, 0], [1, 0, 0], [2, 0, 0]], [[0, 0, 0], [0, 1, 0], [0, 2, 0]])
        raise AssertionError('Collinear camera centers accepted')
    except ValueError:
        pass
    # Non-axis-aligned rotation, with an independently specified orthonormal
    # matrix. This catches covariance direction and quaternion ordering errors.
    rotation_expected = [[0.36, -0.48, 0.8], [0.8, 0.6, 0], [-0.48, 0.64, 0.6]]
    b = [[1.7*x+[3, -4, 5][j] for j, x in enumerate(vector(rotation_expected, p))] for p in a]
    scale, rotation, translation = similarity(a, b)
    assert abs(scale-1.7) < 1e-12
    assert max(abs(rotation[i][j]-rotation_expected[i][j]) for i in range(3) for j in range(3)) < 1e-12
    assert max(abs(x-y) for x, y in zip(translation, [3, -4, 5])) < 1e-12
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)/'cameras.bin'
        path.write_bytes(struct.pack('<QIIQQ4d', 1, 7, 2, 100, 80, 90, 50, 40, 0))
        assert camera_models(path, 1)[7]['focal'] == 90
        path.write_bytes(path.read_bytes()[:-1])
        try:
            camera_models(path, 1)
            raise AssertionError('Truncated calibration accepted')
        except ValueError:
            pass
        # Hamilton world-to-camera +90 degrees about X: C = -R^T t.
        path.write_bytes(struct.pack('<QI7dI', 1, 2, math.sqrt(.5), math.sqrt(.5), 0, 0, 1, 2, 3, 7)+b'frame_000000.png\0'+struct.pack('<Q', 0))
        pose = image_poses(path, {'frame_000000.png'})['frame_000000.png']
        assert max(abs(x-y) for x, y in zip(pose['position'], [-1, -3, 2])) < 1e-12
        expected = [[1, 0, 0], [0, 0, 1], [0, -1, 0]]
        assert max(abs(pose['rotation'][i][j]-expected[i][j]) for i in range(3) for j in range(3)) < 1e-12
        project = Path(directory)/'project'
        (project/'images').mkdir(parents=True)
        (project/'images/frame_000000.png').write_bytes(b'prepared fixture')
        rows = [{'canonicalName': 'frame_000000.png', 'source': '/original/photo.png', 'sourceSha256': 'original-hash', 'heldOut': False}]
        receipt = {'capture': {'frames': [{'preparedPath': 'images/frame_000000.png', 'sourcePath': '/copy/photo.png', 'sourceSha256': 'original-hash'}]}}
        (project/'scene.json').write_text(json.dumps(receipt))
        verify_prepared_mapping(project, rows)
        receipt['capture']['frames'][0]['sourcePath'] = '/copy/wrong-photo.png'
        (project/'scene.json').write_text(json.dumps(receipt))
        try:
            verify_prepared_mapping(project, rows)
            raise AssertionError('Incorrect source-to-camera mapping accepted')
        except ValueError:
            pass
        path = Path(directory)/'images.bin'
        path.write_bytes(struct.pack('<QI7dI', 1, 2, 1, 0, 0, 0, -1, -2, -3, 7)+b'frame_000000.png\0'+struct.pack('<Q', 0))
        assert image_poses(path, {'frame_000000.png'})['frame_000000.png']['position'] == [1, 2, 3]
        try:
            image_poses(path, {'frame_000001.png'})
            raise AssertionError('Held-out/non-training image accepted')
        except ValueError:
            pass
        def model_bytes(names):
            return struct.pack('<Q', len(names))+b''.join(struct.pack('<I7dI', i+1, 1, 0, 0, 0, -i, 0, 0, 7)+name.encode()+b'\0'+struct.pack('<Q', 0) for i, name in enumerate(names))
        names = [f'frame_{i:06d}.png' for i in range(3)]
        numbered = Path(directory)/'numbered'
        for suffix, subset in [('0', names[:1]), ('1', names)]:
            folder = numbered/'sparse'/suffix; folder.mkdir(parents=True)
            (folder/'images.bin').write_bytes(model_bytes(subset))
        models = sparse_components(numbered, names)
        selected, poses = max(models, key=lambda item: len(item[1]))
        assert selected.name == '1' and len(poses) == 3
        direct = Path(directory)/'direct'
        (direct/'sparse').mkdir(parents=True)
        (direct/'sparse/images.bin').write_bytes(model_bytes(names))
        assert len(sparse_components(direct, names)[0][1]) == 3
        (numbered/'sparse/images.bin').write_bytes(model_bytes(names))
        try:
            sparse_components(numbered, names)
            raise AssertionError('Mixed continuation and numbered models accepted')
        except ValueError:
            pass
    print('Photo alignment comparator: similarity, pose convention, degeneracy and binary bounds pass')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--plan', type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--candidate', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--audit-capture', action='store_true', help='Audit one capture against its frozen absolute pose-quality gates; no A/B improvement claim')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not all((args.plan, args.candidate, args.output)) or (not args.audit_capture and not args.baseline) or not args.output.is_absolute() or args.output.exists():
        parser.error('Provide plan, candidate, new absolute output, and either baseline or --audit-capture')
    plan = read_json(args.plan)
    for key, actual in (('baseline', args.baseline), ('candidate', args.candidate)):
        declared = plan.get(key)
        if actual and isinstance(declared, str) and Path(declared).is_absolute() and Path(declared).resolve() != actual.resolve():
            raise ValueError(f'{key} differs from the before-run plan')
    calibration = Path(plan['evaluationCalibration']['path'])
    if digest(calibration) != plan['evaluationCalibration']['sha256']:
        raise ValueError('Reference calibration changed')
    references = reference_cameras(calibration)
    for row in plan['images']:
        if digest(Path(row['source'])) != row['sourceSha256']:
            raise ValueError('A source photo changed')
    if args.audit_capture:
        if plan['schema'] != 'looksy-photo-capture-quality-plan-v1' or args.baseline:
            raise ValueError('Capture audit requires its own before-run quality plan and no baseline')
        training = [r for r in plan['images'] if not r['heldOut']]
        held_out = [r for r in plan['images'] if r['heldOut']]
        training_names, held_names = [{Path(r['source']).stem for r in rows} for rows in (training, held_out)]
        if len(training_names) != len(training) or len(held_names) != len(held_out) or training_names & held_names or \
                len(training) != plan['trainingImageCount'] or len(held_out) != plan['heldOutImageCount']:
            raise ValueError('Capture training/held-out split differs from the frozen plan')
        candidate = evaluate(args.candidate.resolve(), plan['images'], references)
        state = read_json(args.candidate/'scene.json')
        intrinsics = state.get('capture', {}).get('intrinsics', {})
        if state.get('sourceCount') != len(training) or state.get('photoSharedIntrinsics') is not True or \
                state.get('photoStrictMatching') is not True or state.get('focalPriorFrames') != 0 or \
                intrinsics.get('numericPriorSupplied') is not False or len(candidate['cameraModels']) != 1 or 'trainingSchedule' in state:
            raise ValueError('Capture did not use the frozen estimated shared-camera/strict production path before training')
        if set(candidate['database']['features']) != {r['canonicalName'] for r in training}:
            raise ValueError('Feature database includes unexpected or missing training images')
        gates = capture_quality_gates(candidate, plan['qualityGates'])
        result = {'schema': 'looksy-photo-capture-quality-audit-v1', 'planSha256': digest(args.plan), 'candidate': candidate,
                  'gates': gates, 'alignmentQualityPassed': all(gates.values()), 'heldOutImageCount': len(held_out),
                  'heldOutNamesExcluded': True, 'knownNumericCalibrationSupplied': False, 'trainedQualityImprovementQualified': False,
                  'fullReferenceParity': False, 'scope': 'Absolute pose/calibration audit of a new capture; source set differs from the 32-photo experiment, so this is not an isolated algorithm improvement comparison'}
        args.output.mkdir()
        (args.output/'report.json').write_text(json.dumps(result, indent=2)+'\n')
        print(json.dumps({'alignmentQualityPassed': result['alignmentQualityPassed'], 'gates': gates, 'registeredViews': candidate['registeredViews']}, indent=2))
        return
    a, b = [evaluate(p.resolve(), plan['images'], references) for p in (args.baseline, args.candidate)]
    common = sorted({r['canonicalName'] for r in a['images']} & {r['canonicalName'] for r in b['images']})
    if not common:
        raise ValueError('No common registered training cameras')
    fa, fb = [statistics.median(r['focalRelativeError'] for r in result['images'] if r['canonicalName'] in common) for result in (a, b)]
    improvement = 1-fb/fa if fa > 0 else 0
    prepared = sorted(p.name for p in (args.baseline/'images').glob('*.png'))
    same_pixels = prepared == sorted(p.name for p in (args.candidate/'images').glob('*.png')) and all(
        digest(args.baseline/'images'/n) == digest(args.candidate/'images'/n) for n in prepared)
    features_exact = a['database']['features'] == b['database']['features']
    gates = comparison_gates(plan, a, b, improvement, same_pixels, features_exact)
    if set(a['database']['features']) != set(prepared) or set(b['database']['features']) != set(prepared):
        raise ValueError('Feature databases do not contain precisely the prepared training images')
    result = {'schema': 'looksy-photo-intrinsics-comparison-v1', 'planSha256': hashlib.sha256(args.plan.read_bytes()).hexdigest(),
              'baseline': a, 'candidate': b, 'commonRegisteredNames': common, 'commonMedianFocalErrorImprovement': improvement,
              'protocol': plan['schema'], 'featuresExact': features_exact,
              'gates': gates, 'hypothesisPassed': all(gates.values()), 'trainedQualityImprovementQualified': False,
              'privateVideoImprovementQualified': False, 'fullReferenceParity': False,
              'scope': 'Independent reference focal/pose errors after training-camera-center gauge alignment; no held-out images or reference calibration supplied to reconstruction; not a trained photographic-quality metric'}
    args.output.mkdir()
    (args.output/'report.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({'hypothesisPassed': result['hypothesisPassed'], 'gates': gates, 'baselineRegistered': a['registeredViews'],
                      'candidateRegistered': b['registeredViews'], 'commonMedianFocalErrorImprovement': improvement}, indent=2))


if __name__ == '__main__':
    main()
