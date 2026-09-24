#!/usr/bin/env python3
"""Gate the native layer-publication probe's JSONL rows and macOS time receipt."""
import argparse
import json
import math
from pathlib import Path
import re
import statistics


def read(path):
    text = Path(path).read_text()
    rows = [json.loads(line) for line in text.splitlines() if line.startswith('{')]
    rows = [row for row in rows if row.get('event') == 'benchmark.layer_publish.sample']
    if len(rows) < 4 or [row['frame'] for row in rows] != list(range(len(rows))):
        raise ValueError('Missing, duplicated or out-of-order benchmark frames')
    if rows[0]['phase'] != 'cold' or any(row['phase'] != 'warm' for row in rows[1:]):
        raise ValueError('Invalid cold/warm sequence')
    if len({row['sha256'] for row in rows}) != len(rows):
        raise ValueError('Edits did not produce distinct outputs')
    times = sorted(row['elapsed_ms'] for row in rows[1:])
    if any(not math.isfinite(value) or value <= 0 for value in times):
        raise ValueError('Invalid timing sample')
    footprint = re.search(r'(\d+)\s+peak memory footprint', text)
    if not footprint:
        raise ValueError('Missing native peak-memory receipt')
    return rows, {'median_ms': statistics.median(times),
                  'p95_ms': times[math.ceil(len(times)*0.95)-1],
                  'peak_memory_bytes': int(footprint.group(1)),
                  'cold_ms': rows[0]['elapsed_ms'], 'warm_frames': len(times)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--before', required=True)
    parser.add_argument('--after', required=True)
    args = parser.parse_args()
    before_rows, before = read(args.before)
    after_rows, after = read(args.after)
    gain = 1-after['median_ms']/before['median_ms']
    checks = {'median_improves_at_least_20_percent': gain >= 0.20,
              'p95_within_10_percent': after['p95_ms'] <= before['p95_ms']*1.10,
              'peak_memory_within_10_percent': after['peak_memory_bytes'] <= before['peak_memory_bytes']*1.10,
              'every_output_byte_hash_matches': [row['sha256'] for row in before_rows] == [row['sha256'] for row in after_rows],
              'uploads_unchanged': [row['gpu_cpu_to_gpu_bytes'] for row in before_rows] == [row['gpu_cpu_to_gpu_bytes'] for row in after_rows]}
    print(json.dumps({'before': before, 'after': after, 'median_improvement_percent': gain*100,
                      'checks': checks, 'accepted': all(checks.values())}, indent=2))
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
