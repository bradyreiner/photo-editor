#!/usr/bin/env python3
"""Exercise scoped Gitleaks exceptions and ensure synthetic credentials stay detectable.

Requires the pinned Gitleaks CLI; all generated credentials are local random test
strings, never sent to a provider. Reports are private and deleted with the fixture.
"""
import argparse
import base64
from collections import Counter
import hashlib
import hmac
import json
import math
import os
from pathlib import Path
import secrets
import subprocess
import tempfile
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def encoded(value):
    return base64.urlsafe_b64encode(json.dumps(value, separators=(',', ':')).encode()).decode().rstrip('=')


def synthetic_credential(byte_count):
    # Keep positive cases above detector entropy thresholds on every run.
    for _ in range(1000):
        value = secrets.token_hex(byte_count)
        entropy = -sum((count / len(value)) * math.log2(count / len(value))
                       for count in Counter(value).values())
        if entropy >= 3.8:
            return value
    raise AssertionError('Could not construct a synthetic high-entropy credential')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gitleaks', default='gitleaks')
    args = parser.parse_args()
    config = ROOT / '.gitleaks.toml'
    policy = tomllib.loads(config.read_text())
    assert policy['extend']['useDefault'] is True
    assert all(row['condition'] == 'AND' and row['paths'] and row['targetRules'] and row['regexes'] for row in policy['allowlists'])
    with tempfile.TemporaryDirectory(prefix='looksy-secret-policy-') as temporary:
        directory = Path(temporary) / 'corpus'
        directory.mkdir()
        report = Path(temporary) / 'report.json'
        def write(relative, text):
            path = directory / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
            path.chmod(0o600)
        ordinary = 'docs/evidence/policy-control.json'
        journal = 'docs/evidence/app-polish-2026-10-08/recovery-fingerprint-journal.jsonl'
        receipt = 'docs/evidence/agent-conversational-execution-2026-10-06/full-workflow-native-receipt-attempt-13.json'
        model = 'apps/desktop/src/search_model_install.h'
        sony = 'apps/desktop/qml/components/TimelineColorConsole.qml'
        jwt_path = 'tests/fixtures/customer-lease.json'
        thumbnail = next(r for r in policy['allowlists'] if r['targetRules'] == ['sourcegraph-access-token'])['regexes'][0][1:-1]
        write(ordinary, json.dumps({'canonical_plan_cache_key': 'render-plan-v2-fnv1a64-' + secrets.token_hex(8),
                                   'sampleCanonicalRenderPlanCacheKey': 'render-plan-v2-fnv1a64-' + secrets.token_hex(8),
                                   'customer_access_controller.h': secrets.token_hex(32)}))
        write(journal, json.dumps({'previousEntryHash': secrets.token_hex(32)}, separators=(',', ':')))
        write(receipt, json.dumps({'service': 'sourcegraph', 'thumbnailInvalidationKey': 'sha1:' + thumbnail}))
        write(model, '{"tokenizer.json", "' + secrets.token_hex(32) + '", 1234};\n')
        write(sony, '{"key": "slog3-sgamut3-cine"}\n')
        fixture = json.loads((ROOT / jwt_path).read_text())
        write(jwt_path, json.dumps(fixture))
        def scan():
            # Create the report ourselves so its permissions never depend on umask.
            descriptor = os.open(report, os.O_CREAT | os.O_TRUNC | os.O_WRONLY, 0o600)
            os.close(descriptor)
            result = subprocess.run([args.gitleaks, 'dir', '--config', str(config), '--redact', '--no-banner',
                                     '--report-format=json', '--report-path', str(report), '.'],
                                    cwd=directory, capture_output=True, text=True, timeout=60)
            if result.returncode not in (0, 1):
                raise RuntimeError('Gitleaks could not execute the policy check')
            findings = json.loads(report.read_text())
            report.unlink()  # Never rescan a generated report as fixture input.
            return result.returncode, findings
        status, findings = scan()
        if status or findings:
            raise AssertionError('Known non-secret fixture families must pass: ' + str([(r['RuleID'], r['File']) for r in findings]))
        for relative, field, value in [
            (ordinary, 'service_api_key', synthetic_credential(32)),
            (journal, 'sentry_auth_token', synthetic_credential(32)),
            (receipt, 'sourcegraph_access_token', 'sgp_' + synthetic_credential(20)),
            (receipt, 'sourcegraph_legacy_token', synthetic_credential(20)),
            (model, 'service_api_key', synthetic_credential(32)),
            (sony, 'service_api_key', synthetic_credential(32)),
        ]:
            with (directory / relative).open('a') as output:
                output.write('\n' + field + '="' + value + '"\n')
        message = encoded({'alg': 'HS256', 'typ': 'JWT'}) + '.' + encoded({
            'iss': 'https://accounts.production-policy-test.test', 'aud': 'customer-session',
            'sub': secrets.token_hex(16), 'iat': 2000000000, 'exp': 2000000600})
        token = message + '.' + base64.urlsafe_b64encode(hmac.digest(secrets.token_bytes(32), message.encode(), hashlib.sha256)).decode().rstrip('=')
        fixture['tokens']['unexpectedProductionShapedSession'] = token
        write(jwt_path, json.dumps(fixture))
        status, findings = scan()
        observed = {next((path for path in (ordinary, journal, receipt, model, sony, jwt_path) if r['File'].endswith(path)), '') for r in findings}
        expected = {ordinary, journal, receipt, model, sony, jwt_path}
        if status != 1 or observed != expected:
            raise AssertionError('Synthetic credentials must remain detectable in every exception path')
        sourcegraph_findings = [r for r in findings if r['RuleID'] == 'sourcegraph-access-token']
        if len(sourcegraph_findings) != 2:
            raise AssertionError('Both prefixed and bare legacy Sourcegraph credentials must remain detectable')
        jwt_findings = [r for r in findings if r['RuleID'] == 'jwt']
        if len(jwt_findings) != 1 or not jwt_findings[0]['File'].endswith(jwt_path):
            raise AssertionError('JWT exception must match only the exact public test values')
        print('Secret scanner policy passed: seven non-secret families accepted; synthetic credentials detected in all six exception paths.')


if __name__ == '__main__':
    main()
