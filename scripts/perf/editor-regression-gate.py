#!/usr/bin/env python3
"""Enforce the editor regression contract; skipped/missing proof never passes."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import re
import statistics
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / 'tests/fixtures/editor-regression/contract.json'
SCHEMA = 'looksy-editor-regression-report-v1'


class HostUnqualified(ValueError):
    """The machine cannot supply required native or performance evidence."""


def check_desktop_session(sample):
    if sample.get('screenLocked') is True:
        raise HostUnqualified('macOS screen is locked; native foreground qualification requires an unlocked desktop')


def check_host_load(load_average, logical_cpus, maximum_ratio):
    if (type(load_average) not in (int, float) or not math.isfinite(load_average) or load_average < 0 or
            type(logical_cpus) is not int or logical_cpus <= 0 or
            type(maximum_ratio) not in (int, float) or not math.isfinite(maximum_ratio) or not 0 < maximum_ratio <= .5):
        raise HostUnqualified('host load telemetry is missing or invalid')
    if load_average > logical_cpus * maximum_ratio:
        raise HostUnqualified(f'host load {load_average:.2f} exceeds idle-run limit '
                              f'{logical_cpus * maximum_ratio:.2f}; no performance qualification')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def write(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def percentile(values, fraction=0.95):
    require(bool(values) and all(type(x) in (int, float) and math.isfinite(x) and x >= 0 for x in values),
            'latency samples must be finite, nonnegative, and nonempty')
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)]


def check_junit(path, expected):
    cases = list(ET.parse(path).getroot().iter('testcase'))
    names = [case.get('name') for case in cases]
    require(len(names) == len(set(names)), 'duplicate JUnit test names')
    require(set(names) == set(expected), f'JUnit inventory mismatch: missing={set(expected)-set(names)}, extra={set(names)-set(expected)}')
    for case in cases:
        require(case.get('status') in (None, 'run') and
                not any(child.tag in {'skipped', 'failure', 'error'} for child in case),
                f'{case.get("name")}: skipped, not run, or failed')
    return names


def check_performance(samples, policy):
    require(set(samples) == set(policy['budgets']), 'performance scenario inventory mismatch')
    summary = {}
    allowance = policy.get('max_regression_fraction')
    if allowance is not None:
        require(type(allowance) in (int, float) and math.isfinite(allowance) and 0 <= allowance <= .05,
                'baseline regression allowance exceeds 5%')
        require(policy['peak_rss_max_bytes'] <= policy['baseline_peak_rss_bytes'] * (1 + allowance),
                'peak memory budget exceeds baseline allowance')
    for name, budget in policy['budgets'].items():
        if allowance is not None:
            for field, baseline in [('median_ms', 'baseline_median_ms'), ('p95_ms', 'baseline_p95_ms')]:
                require(math.isclose(budget[field], budget[baseline] * (1 + allowance), rel_tol=1e-9),
                        f'{name}: budget no longer matches its reviewed baseline')
        rows = samples[name]
        require(len(rows) == policy['samples'], f'{name}: wrong replicate count')
        for row in rows:
            require(set(row) == {'candidate_ms', 'reference_ms'}, f'{name}: invalid metric fields')
            require(all(type(x) in (int, float) and math.isfinite(x) and x > 0 for x in row.values()),
                    f'{name}: invalid/nonpositive timing')
        candidate = [x['candidate_ms'] for x in rows]
        ratios = [x['candidate_ms'] / x['reference_ms'] for x in rows]
        median, p95, ratio = statistics.median(candidate), percentile(candidate), statistics.median(ratios)
        require(median <= budget['median_ms'], f'{name}: median {median} exceeds {budget["median_ms"]} ms')
        require(p95 <= budget['p95_ms'], f'{name}: replicate p95 {p95} exceeds {budget["p95_ms"]} ms')
        require(ratio <= policy['max_candidate_reference_ratio'], f'{name}: optimization advantage lost ({ratio:.3f} ratio)')
        summary[name] = {'median_ms': median, 'replicate_median_p95_ms': p95, 'candidate_reference_ratio': ratio}
    return summary


def metric(output, marker, candidate, reference, scale=1.0):
    lines = [line for line in output.splitlines() if line.startswith(marker)]
    require(len(lines) == 1, f'{marker}: missing or duplicate benchmark result')
    fields = dict(re.findall(r'(\w+)=([^\s]+)', lines[0]))
    result = {'candidate_ms': float(fields[candidate]) * scale,
              'reference_ms': float(fields[reference]) * scale}
    require(all(math.isfinite(x) and x > 0 for x in result.values()), 'invalid benchmark timing')
    return result


def check_raw_color_preview(output, expected_sha256=None):
    """Require actual combined-exposure, reopen/reset/cancel cases on each RAW."""
    if expected_sha256 is not None:
        require(isinstance(expected_sha256, str) and re.fullmatch(r'[0-9a-f]{64}', expected_sha256),
                'RAW color preview: invalid expected source SHA-256')
        fixture_lines = [line for line in output.splitlines()
                         if line.startswith('raw_exposure_fixture ')]
        require(len(fixture_lines) == 1, 'RAW color preview: missing/duplicate source fixture marker')
        fixture = re.fullmatch(r'raw_exposure_fixture sha256=([0-9a-f]{64})', fixture_lines[0])
        require(fixture is not None and fixture.group(1) == expected_sha256,
                'RAW color preview: wrong source fixture SHA-256')
    marker = 'raw_exposure_combined_preview_parity '
    lines = [line for line in output.splitlines() if line.startswith(marker)]
    expected = {(draft, transform, light) for draft in (0, 1)
                for transform in (0, 1) for light in (0.5, -1.9)}
    seen = set()
    require(len(lines) == len(expected), 'RAW color preview: missing/duplicate cases')
    for line in lines:
        pairs = re.findall(r'(\w+)=([^\s]+)', line)
        fields = dict(pairs)
        require(len(pairs) == 8 and set(fields) == {'draft', 'input_transform', 'raw_ev', 'light_ev', 'total_ev',
                                'reopened', 'reset', 'cancelled'}, 'RAW color preview: invalid fields')
        case = (int(fields['draft']), int(fields['input_transform']), float(fields['light_ev']))
        require(case in expected and case not in seen, 'RAW color preview: unexpected/duplicate case')
        require(float(fields['raw_ev']) == 1.9 and
                math.isclose(float(fields['total_ev']), 1.9 + case[2], abs_tol=1e-12),
                'RAW color preview: wrong exposure intent')
        require(all(fields[key] == '1' for key in ('reopened', 'reset', 'cancelled')),
                'RAW color preview: lifecycle proof missing')
        seen.add(case)
    return len(seen)


def contract_tests(contract, tier):
    core, hardware, recovery = (contract[k] for k in ('core_tests', 'hardware_tests', 'recovery_tests'))
    require(all(isinstance(items, list) and items and len(items) == len(set(items)) and
                all(isinstance(s, str) and re.fullmatch(r'[a-z0-9_]+', s) for s in items)
                for items in (core, hardware, recovery)), 'invalid test inventory')
    require(set(hardware) <= set(core) and not set(core) & set(recovery), 'invalid tier membership')
    if tier == 'portable':
        return [s for s in core if s not in hardware] + recovery
    if tier == 'hardware':
        return hardware
    return core + recovery


def exploration_cases(contract, seeds=None, steps=None, commit=None):
    policy = contract['workflow_exploration']
    require(policy['test'] in contract['core_tests'], 'exploration test is not enforced')
    selected = list(policy['seeds'] if seeds is None else seeds)
    # Every revision explores another reproducible sequence in addition to the
    # small pinned regression corpus. Explicit seeds reproduce a failure exactly.
    if seeds is None and commit:
        selected.append(int(commit[:8], 16))
    count = policy['steps'] if steps is None else steps
    require(type(count) is int and 1 <= count <= 500, 'exploration steps must be 1..500')
    require(selected and len(selected) <= 64 and all(type(seed) is int and 0 <= seed <= 0xffffffff
            for seed in selected), 'exploration requires 1..64 unsigned 32-bit seeds')
    return [(seed, count) for seed in dict.fromkeys(selected)]


def check_exploration(output, seed, steps):
    expected = f'authoring_sequence seed={seed} steps={steps} status=passed'
    rows = [line for line in output.splitlines() if line.startswith('authoring_sequence ')]
    require(rows == [expected], 'exploration did not complete the requested sequence')
    markers = [line[len('authoring_coverage '):] for line in output.splitlines()
               if line.startswith('authoring_coverage ')]
    require(len(markers) == 1, 'exploration coverage is missing or duplicated')
    coverage = json.loads(markers[0])
    require(isinstance(coverage, dict) and type(coverage.get('seed')) is int and
            type(coverage.get('steps')) is int and coverage['seed'] == seed and coverage['steps'] == steps,
            'exploration coverage belongs to another sequence')
    operations = coverage.get('operations')
    require(isinstance(operations, list) and len(operations) == 20 and all(isinstance(row, dict) for row in operations),
            'exploration operation inventory missing')
    require({row.get('action') for row in operations} == set(range(20)), 'exploration operation inventory differs')
    for row in operations:
        require(all(type(row.get(key)) is int and row[key] >= 0
                    for key in ('action', 'attempted', 'applied', 'rejected', 'eligible')),
                'invalid exploration operation counts')
        require(row['attempted'] == row['applied'] + row['rejected'] and row['eligible'] <= row['applied'],
                'eligible operations did not apply or coverage counts disagree')
    for key in ('attempted', 'applied', 'rejected'):
        require(type(coverage.get(key)) is int and coverage[key] == sum(row[key] for row in operations),
                'exploration aggregate coverage differs')
    require(coverage['attempted'] == steps, 'exploration omitted operation attempts')
    require(coverage['applied'] > 0, 'exploration applied no authoring operations')
    require(type(coverage.get('inverseProbes')) is int and 0 <= coverage['inverseProbes'] <= steps and
            type(coverage.get('lockedRejections')) is int and 0 <= coverage['lockedRejections'] <= coverage['rejected'],
            'invalid exploration inverse/lock coverage')
    if steps >= 20:
        require(all(row['applied'] > 0 and row['eligible'] > 0 for row in operations if row['action'] not in (10, 11)) and
                coverage['inverseProbes'] > 0 and coverage['lockedRejections'] > 0,
                'exploration did not exercise its required authoring categories')
    return coverage


def cache_values(build):
    values = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        match = re.match(r'([^/#][^:]*):[^=]*=(.*)', line)
        if match:
            values[match[1]] = match[2]
    return values


class Gate:
    def __init__(self, build, output, tier):
        self.build, self.output, self.tier = build.resolve(), output.resolve(), tier
        require(not self.output.exists(), 'output directory must be fresh; stale proof cannot be reused')
        self.output.mkdir(parents=True)
        self.contract = load(CONTRACT)
        self.cache = cache_values(self.build)
        require(Path(self.cache['CMAKE_HOME_DIRECTORY']).resolve() == ROOT, 'build belongs to a different checkout')
        self.checks = []
        self.executable_hashes = {}
        self.counter = 0
        self.env = {k: v for k, v in os.environ.items() if not k.startswith('PHOTO_')}
        self.env.update(QT_QPA_PLATFORM='offscreen', PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE='1')
        for key, folder in [('PHOTO_EDITOR_APP_DATA_DIR', 'app-data'), ('PHOTO_EDITOR_CACHE_DIR', 'cache'),
                            ('PHOTO_EDITOR_BACKUP_DIR', 'backups'), ('PHOTO_EDITOR_MANAGED_MEDIA_DIR', 'managed')]:
            self.env[key] = str(self.output / 'runtime' / folder)
        self.report = {'schema': SCHEMA, 'status': 'failed', 'tier': tier,
                       'contract_sha256': sha(CONTRACT), 'commit_sha': self.git('rev-parse', 'HEAD'),
                       'source_clean': not self.git('status', '--porcelain', '--untracked-files=all'),
                       'system': platform.system(), 'machine': platform.machine(),
                       'configuration': self.cache.get('CMAKE_BUILD_TYPE'), 'checks': self.checks,
                       'executable_sha256': self.executable_hashes}

    def git(self, *args):
        return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()

    def run(self, name, command, env=None, timeout=600):
        self.counter += 1
        log = self.output / f'{self.counter:03d}-{name}.log'
        with log.open('w') as handle:
            result = subprocess.run([str(x) for x in command], cwd=ROOT, env=env or self.env,
                                    stdout=handle, stderr=subprocess.STDOUT, timeout=timeout, check=False)
        self.checks.append({'name': name, 'exit_code': result.returncode, 'log': log.name})
        require(result.returncode == 0, f'{name}: exit {result.returncode}; see {log}')
        return log.read_text()

    def executable(self, name):
        path = self.build / 'tests' / (name + ('.exe' if os.name == 'nt' else ''))
        require(path.is_file(), f'required executable missing: {path}')
        self.executable_hashes[name] = sha(path)
        return path

    def preflight(self):
        require(self.contract['schema'] == 'looksy-editor-regression-contract-v1', 'unsupported contract')
        for file, expected in self.contract['fixture_sha256'].items():
            require(sha(ROOT / file) == expected, f'photographic fixture changed: {file}')
        require(self.cache.get('PHOTO_EDITOR_BUILD_DESKTOP') == 'ON', 'desktop build required')
        if self.tier in {'performance', 'native'}:
            require(self.cache.get('CMAKE_BUILD_TYPE') == 'Release', 'performance qualification requires Release')
            profile = self.contract['performance']['profile']
            require(platform.system() == profile['system'] and platform.machine() == profile['machine'],
                    'unqualified hardware profile; do not reuse another machine\'s budgets')
            cpu = self.run('cpu-profile', ['/usr/sbin/sysctl', '-n', 'machdep.cpu.brand_string']).strip()
            require(cpu == profile['cpu'], f'expected {profile["cpu"]}, found {cpu}')
            self.report['cpu'] = cpu
            require(os.cpu_count() == profile['logical_cpus'], 'wrong logical CPU count for this hardware baseline')
            self.host_sample()
        if self.tier == 'native':
            require(self.report['source_clean'], 'native release evidence requires a clean committed checkout')
            source_dir = self.env.get('LOOKSY_REAL_SHOOT_SOURCE_DIR', '')
            require(bool(source_dir), 'LOOKSY_REAL_SHOOT_SOURCE_DIR must stage the three pinned CR2 fixtures')
            source_dir = Path(source_dir)
            expected = self.contract['native']['real_shoot_fixture_sha256']
            require({p.name for p in source_dir.iterdir() if p.is_file() and p.suffix.lower() == '.cr2'} == set(expected),
                    'real-shoot RAW fixture inventory differs')
            require(all(sha(source_dir / name) == digest for name, digest in expected.items()),
                    'real-shoot RAW fixture hash differs')
            app = self.build / 'apps/desktop/Looksy.app/Contents/MacOS/Looksy'
            self.executable_hashes['Looksy'] = sha(app)

    def host_sample(self):
        try:
            value = {'one_minute': os.getloadavg()[0], 'logical_cpus': os.cpu_count()}
        except OSError as error:
            raise HostUnqualified('host load unavailable') from error
        self.report.setdefault('host_load_samples', []).append(value)
        check_host_load(value['one_minute'], value['logical_cpus'],
                        self.contract['performance']['max_host_load_per_cpu'])

    def correctness(self):
        names = contract_tests(self.contract, self.tier)
        inventory = json.loads(self.run('inventory', ['ctest', '--test-dir', self.build, '--show-only=json-v1']))
        tests = {t['name']: t for t in inventory['tests']}
        require(set(names) <= set(tests), f'required tests missing: {set(names)-set(tests)}')
        for name in names:
            properties = {p['name']: p['value'] for p in tests[name].get('properties', [])}
            require(not properties.get('DISABLED'), f'required test disabled: {name}')
            command = tests[name].get('command', [])
            require(bool(command), f'{name}: missing test command')
            self.executable_hashes[name] = sha(command[0])
        junit = self.output / 'correctness.xml'
        expression = '^(' + '|'.join(re.escape(s) for s in names) + ')$'
        self.run('correctness', ['ctest', '--test-dir', self.build, '-R', expression, '-j', '1',
                                '--no-tests=error', '--output-on-failure', '--output-junit', junit])
        self.report['tests'] = check_junit(junit, names)
        if self.tier == 'native':
            for file_name in sorted(self.contract['native']['real_shoot_fixture_sha256']):
                output = self.run(
                    'native-raw-color-' + Path(file_name).stem.lower(),
                    [self.executable('preview_linear_cache_test'), '--raw-exposure',
                     Path(self.env['LOOKSY_REAL_SHOOT_SOURCE_DIR']) / file_name], timeout=180)
                check_raw_color_preview(
                    output, self.contract['native']['real_shoot_fixture_sha256'][file_name])

    def explore(self, seeds=None, steps=None):
        cases = exploration_cases(self.contract, seeds, steps, self.report['commit_sha'])
        self.report['exploration'] = []
        for seed, count in cases:
            command = [self.executable(self.contract['workflow_exploration']['test']), seed, count]
            row = {'seed': seed, 'steps': count, 'status': 'failed',
                   'reproduce': [str(value) for value in command]}
            self.report['exploration'].append(row)
            output = self.run(f'authoring-seed-{seed}', command, timeout=180)
            row['coverage'] = check_exploration(output, seed, count)
            row['status'] = 'passed'

    def performance(self):
        cases = {
            'icc': ('render_executor_icc_parallel_test', [], 'CPU ICC transform benchmark:', 'parallel_median_us', 'serial_median_us', .001),
            'brush_commit': ('photo_paint_commit_test', ['--benchmark'], 'brush_commit_benchmark ', 'candidate_median_ms', 'reference_median_ms', 1),
            'selection': ('brush_rasterizer_test', [], 'selection_live_raster_benchmark ', 'candidate_ms', 'legacy_ms', 1),
            'slider': ('preview_linear_cache_test', [], 'preview_linear_cache_benchmark ', 'candidate_median_ms', 'reference_median_ms', 1),
            'styled_text': ('photo_pixel_paint_test', ['--text-transform-icc-parity'], 'Text transform ICC parity:', 'parallel_wall_median_ms', 'serial_wall_median_ms', 1),
        }
        samples = {name: [] for name in cases}
        self.report['performance_samples'] = samples
        count = self.contract['performance']['samples']
        require(count >= 5, 'at least five benchmark replicates required')
        for iteration in range(count):
            # Reverse alternate rounds so fixed process order cannot favor a case.
            order = list(cases) if iteration % 2 == 0 else list(reversed(cases))
            for name in order:
                self.host_sample()
                exe, args, marker, candidate, reference, scale = cases[name]
                output = self.run(f'{name}-{iteration}', [self.executable(exe), *args])
                samples[name].append(metric(output, marker, candidate, reference, scale))
        self.report['performance_samples'] = samples
        self.host_sample()
        self.report['performance_summary'] = check_performance(samples, self.contract['performance'])
        usage = self.run('brush-rss', ['/usr/bin/time', '-l', self.executable('photo_paint_commit_test'), '--benchmark'])
        matches = re.findall(r'^\s*(\d+)\s+maximum resident set size\s*$', usage, re.MULTILINE)
        require(len(matches) == 1, 'missing peak RSS proof')
        rss = int(matches[0])
        require(0 < rss <= self.contract['performance']['peak_rss_max_bytes'], 'brush peak RSS exceeds budget')
        self.report['brush_peak_rss_bytes'] = rss

    def finish(self, error=None):
        if self.tier == 'native' and error is None:
            require(not self.git('status', '--porcelain', '--untracked-files=all') and
                    self.git('rev-parse', 'HEAD') == self.report['commit_sha'] and
                    sha(CONTRACT) == self.report['contract_sha256'],
                    'source or contract changed during native qualification')
        self.report['status'] = 'unqualified' if isinstance(error, HostUnqualified) else ('failed' if error else 'passed')
        self.report['release_eligible'] = self.tier == 'native' and not error
        if error:
            self.report['error'] = str(error)
        # Runtime data is isolated and never part of the proof archive.
        self.report['files'] = {str(p.relative_to(self.output)): sha(p) for p in self.output.rglob('*')
                                if p.is_file() and 'runtime' not in p.relative_to(self.output).parts and p != self.output / 'report.json'}
        write(self.output / 'report.json', self.report)


def validate_report(directory, commit):
    directory = directory.resolve()
    report = load(directory / 'report.json')
    require(report.get('schema') == SCHEMA and report.get('status') == 'passed' and
            report.get('release_eligible') is True, 'regression proof not release-qualified')
    require(report.get('commit_sha') == commit and report.get('source_clean') is True, 'stale or dirty-source proof')
    require(report.get('tier') == 'native' and report.get('configuration') == 'Release', 'native Release qualification required')
    require(report.get('contract_sha256') == sha(CONTRACT), 'proof uses another regression contract')
    contract = load(CONTRACT)
    require(set(report.get('tests', [])) == set(contract_tests(contract, 'native')), 'incomplete correctness proof')
    require(report.get('checks') and all(c.get('exit_code') == 0 for c in report['checks']), 'failed proof command')
    expected_commands = {'cpu-profile', 'inventory', 'correctness', 'brush-rss',
                         'native-text', 'native-slider', 'native-selection', 'native-paint',
                         'native-eraser', 'native-crop', 'native-workflow', 'native-real-shoot'}
    expected_commands.update({'native-text-authoring', 'native-selection-authoring', 'native-board-source', 'real-inpaint-controller'})
    expected_commands.update(f'{name}-{i}' for name in contract['performance']['budgets']
                             for i in range(contract['performance']['samples']))
    raw_color_commands = {'native-raw-color-' + Path(name).stem.lower(): digest
                          for name, digest in contract['native']['real_shoot_fixture_sha256'].items()}
    expected_commands.update(raw_color_commands)
    names = [c.get('name') for c in report['checks']]
    require(len(names) == len(set(names)) and set(names) == expected_commands,
            'missing/duplicate qualification commands')
    require(report.get('system') == 'Darwin' and report.get('machine') == 'arm64' and
            report.get('cpu') == contract['performance']['profile']['cpu'], 'wrong proof machine')
    check_performance(report.get('performance_samples', {}), contract['performance'])
    host_samples = report.get('host_load_samples', [])
    require(len(host_samples) == contract['performance']['samples'] * len(contract['performance']['budgets']) + 2,
            'missing host-load qualification samples')
    for sample in host_samples:
        require(sample.get('logical_cpus') == contract['performance']['profile']['logical_cpus'], 'wrong proof CPU count')
        check_host_load(sample.get('one_minute'), sample['logical_cpus'], contract['performance']['max_host_load_per_cpu'])
    require(type(report.get('brush_peak_rss_bytes')) is int and
            0 < report['brush_peak_rss_bytes'] <= contract['performance']['peak_rss_max_bytes'], 'missing/invalid peak memory')
    require(report.get('native_checks') is not None, 'missing native proof')
    from editor_native_regression import validate_native
    validate_native(directory, report['native_checks'], contract['native'])
    files = report.get('files', {})
    require(files and 'correctness.xml' in files, 'missing proof files')
    require(all(c.get('log') in files for c in report['checks']), 'missing command logs')
    identities = report.get('executable_sha256', {})
    required_executables = set(contract_tests(contract, 'native')) | {
        'Looksy', 'desktop_shell_studio_polish_test', 'actual_app_workflow_benchmark_driver',
        'photo_paint_performance_test', 'desktop_shell_photo_tools_test', 'desktop_shell_real_shoot_workflow_test',
        'desktop_shell_mask_reselection_pointer_test', 'editor_real_inpaint_workflow_test'}
    require(required_executables <= set(identities) and
            all(isinstance(h, str) and re.fullmatch('[0-9a-f]{64}', h) for h in identities.values()),
            'missing/invalid executable identities')
    actual_files = {str(p.relative_to(directory)) for p in directory.rglob('*')
                    if p.is_file() and 'runtime' not in p.relative_to(directory).parts and p != directory / 'report.json'}
    require(set(files) == actual_files, 'proof file inventory changed')
    for name, expected in files.items():
        path = directory / name
        require(not Path(name).is_absolute() and '..' not in Path(name).parts and
                path.resolve().is_relative_to(directory) and not path.is_symlink(), 'unsafe proof path')
        require(sha(path) == expected, f'tampered or missing evidence: {name}')
    for check in report['checks']:
        if check['name'] in raw_color_commands:
            check_raw_color_preview((directory / check['log']).read_text(),
                                    raw_color_commands[check['name']])
    check_junit(directory / 'correctness.xml', contract_tests(contract, 'native'))
    require(report.get('executable_sha256', {}).get('Looksy'), 'missing app identity')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--tier', choices=['portable', 'hardware', 'performance', 'native', 'exploration'], default='portable')
    parser.add_argument('--seed', type=int, action='append', help='Reproduce an exploration seed; repeat for a corpus')
    parser.add_argument('--steps', type=int, help='Exploration journey length, 1..500')
    parser.add_argument('--allow-visible', action='store_true', help='Explicitly permit disposable native UI checks')
    parser.add_argument('--validate', type=Path, help='Validate a downloaded native proof bundle')
    parser.add_argument('--commit', help='Required exact commit for downloaded proof')
    args = parser.parse_args()
    gate = None
    try:
        if args.validate:
            require(bool(re.fullmatch(r'[0-9a-f]{40}', args.commit or '')), 'exact commit SHA required')
            validate_report(args.validate, args.commit)
            print('editor_regression_release_eligible=true')
            return 0
        require(args.build_dir and args.output, '--build-dir and --output required')
        require(args.tier == 'exploration' or (args.seed is None and args.steps is None),
                '--seed and --steps require --tier exploration')
        require(args.tier != 'native' or args.allow_visible, 'native checks require --allow-visible; no windows opened')
        gate = Gate(args.build_dir, args.output, args.tier)
        if args.tier == 'native':
            from editor_native_regression import desktop_session
            gate.report['desktop_session'] = desktop_session()
            check_desktop_session(gate.report['desktop_session'])
        gate.preflight()
        if args.tier not in {'performance', 'exploration'}:
            gate.correctness()
        if args.tier == 'exploration':
            gate.explore(args.seed, args.steps)
        if args.tier in {'performance', 'native'}:
            gate.performance()
        if args.tier == 'native':
            from editor_native_regression import collect_native
            gate.report['native_checks'] = collect_native(gate)
        gate.finish()
        print(f'editor_regression_gate=passed report={gate.output / "report.json"}')
        return 0
    except (ValueError, OSError, KeyError, TypeError, ET.ParseError, subprocess.SubprocessError) as error:
        if gate:
            gate.finish(error)
        status = gate.report['status'] if gate else 'failed'
        print(f'editor_regression_gate={status}: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
