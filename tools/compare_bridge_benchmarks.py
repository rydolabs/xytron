#!/usr/bin/env python3
"""Aggregate sequential CEF/WebView2 samples from ONE Windows Actions runner.

No performance threshold is enforced: this is a reproducible paired
experiment with changed engine order per round, not a product SLA.
"""
import argparse
import json
from pathlib import Path
import statistics


SCENARIOS = (
    'LAT-01-direct', 'LAT-02-invoke', 'LAT-04-event-ack',
    'LAT-05-binary-1024', 'LAT-05-binary-65536',
    'LAT-06-parallel-8', 'LAT-06-parallel-32'
)


def read_round(root, number, engine):
    directory = root / ('round-%d-%s' % (number, engine))
    paths = list(directory.glob('*-summary.json'))
    if len(paths) != 1:
        raise ValueError('Expected exactly one summary in ' + str(directory))
    data = json.loads(paths[0].read_text(encoding='utf-8'))
    metadata = data['metadata']
    if metadata['engine'] != engine or metadata['os'] != 'Windows':
        raise ValueError('Wrong engine/OS in ' + str(paths[0]))
    samples = {record['id']: record for record in data['results']}
    if not all(name in samples for name in SCENARIOS):
        raise ValueError('Missing benchmark scenario in ' + str(paths[0]))
    return metadata, samples


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--rounds', type=int, default=3)
    args = parser.parse_args()
    if args.rounds < 2:
        parser.error('At least two alternating-order rounds are required')
    observations = []
    meta = []
    for round_number in range(1, args.rounds + 1):
        cef_meta, cef = read_round(args.root, round_number, 'cef')
        web_meta, web = read_round(args.root, round_number, 'webview2')
        meta.extend([cef_meta, web_meta])
        for scenario in SCENARIOS:
            a = cef[scenario]
            b = web[scenario]
            observations.append({
                'round': round_number,
                'scenario': scenario,
                'cef_p50_ms': a['p50_ms'],
                'webview2_p50_ms': b['p50_ms'],
                'cef_p95_ms': a['p95_ms'],
                'webview2_p95_ms': b['p95_ms'],
                'p50_ratio_webview2_over_cef': (
                    b['p50_ms'] / a['p50_ms'] if a['p50_ms'] else None),
            })
    shas = {m['github_sha'] for m in meta}
    if len(shas) != 1:
        raise ValueError('Paired results are not from the same commit')
    machines = {(m['os'], m['cpu'], m['runner_image']) for m in meta}
    if len(machines) != 1:
        raise ValueError('Paired results have inconsistent runner metadata')

    lines = [
        '# Xytron CEF vs WebView2: same Windows runner',
        '',
        'Repeated sequential test, alternating engine order; not a latency SLA.',
        'Each pair uses the same GitHub job VM, OS, CPU, commit and build setup.',
        'Measurements are not truly simultaneous and still contain run-order noise.',
        '',
        'Commit: ' + (next(iter(shas)) or 'local'),
        '',
        '| Scenario | CEF p50 median (ms) | WebView2 p50 median (ms) | '
        'WebView2/CEF p50 ratio (median) |',
        '| --- | ---: | ---: | ---: |'
    ]
    rollup = []
    for scenario in SCENARIOS:
        rows = [x for x in observations if x['scenario'] == scenario]
        cef_med = statistics.median(x['cef_p50_ms'] for x in rows)
        web_med = statistics.median(x['webview2_p50_ms'] for x in rows)
        ratios = [x['p50_ratio_webview2_over_cef'] for x in rows]
        ratio = statistics.median(x for x in ratios if x is not None) if any(
            x is not None for x in ratios) else None
        rollup.append({
            'scenario': scenario, 'cef_p50_median_ms': cef_med,
            'webview2_p50_median_ms': web_med,
            'webview2_over_cef_median_ratio': ratio,
            'cef_p50_by_round_ms': [x['cef_p50_ms'] for x in rows],
            'webview2_p50_by_round_ms': [x['webview2_p50_ms'] for x in rows],
        })
        lines.append('| %s | %.3f | %.3f | %s |' % (
            scenario, cef_med, web_med,
            '%.2fx' % ratio if ratio is not None else 'n/a'))
    args.root.mkdir(parents=True, exist_ok=True)
    (args.root / 'paired-summary.json').write_text(
        json.dumps({'schema': 1, 'metadata': meta, 'rounds': observations,
                    'rollup': rollup}, indent=2) + '\n', encoding='utf-8')
    (args.root / 'paired-summary.md').write_text(
        '\n'.join(lines) + '\n', encoding='utf-8')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
