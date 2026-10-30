#!/usr/bin/env python3
"""Run a real browser Xytron bridge latency smoke and retain raw samples."""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import platform
import signal
import statistics
import subprocess
import sys
from datetime import datetime, timezone


def percentile(values, q):
    ordered = sorted(values)
    pos = (len(ordered) - 1) * q
    lo = math.floor(pos)
    hi = math.ceil(pos)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (pos - lo)


def run():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--engine', required=True, choices=('cef', 'webview2', 'wkwebview'))
    p.add_argument('--build-dir', type=Path, required=True)
    p.add_argument('--output-dir', type=Path, required=True)
    p.add_argument('--warmup', type=int, default=20)
    p.add_argument('--samples', type=int, default=100)
    p.add_argument('--timeout', type=int, default=120)
    args = p.parse_args()
    if not 0 <= args.warmup <= 5000 or not 10 <= args.samples <= 5000:
        p.error('warmup must be 0..5000; samples must be 10..5000')
    if args.engine == 'webview2' and os.name != 'nt':
        p.error('WebView2 requires Windows')
    if args.engine == 'wkwebview' and sys.platform != 'darwin':
        p.error('WKWebView requires macOS')
    project = Path(__file__).resolve().parent.parent
    html = (project / 'tests/integration/cef/bridge_benchmark.html').resolve()
    exe = ('nativeweb_wkwebview_benchmark' if args.engine == 'wkwebview'
           else 'nativeweb_' + args.engine + '_bridge_integration')
    if os.name == 'nt':
        exe += '.exe'
    executable = (args.build_dir.resolve() /
                  ('' if args.engine == 'wkwebview' else 'bin') / exe)
    if not html.is_file() or not executable.is_file():
        p.error('Missing actual browser executable or HTML: ' + str(executable))

    if args.engine == 'wkwebview':
        command = [str(executable), str(html)]
    else:
        command = [str(executable), '--url=' + html.as_uri()]
        if args.engine == 'cef':
            command.insert(1, '--no-sandbox')
    if args.engine == 'cef' and sys.platform.startswith('linux') and not os.getenv('DISPLAY'):
        from shutil import which
        if not which('xvfb-run'):
            p.error('xvfb-run required for Linux CEF')
        command = ['xvfb-run', '-a'] + command
    env = os.environ.copy()
    env['XYTRON_BENCH_WARMUP'] = str(args.warmup)
    env['XYTRON_BENCH_SAMPLES'] = str(args.samples)
    print('Bridge benchmark', args.engine, 'samples', args.samples, flush=True)
    kw = {'env': env, 'stdout': subprocess.PIPE,
          'stderr': subprocess.STDOUT, 'text': True}
    if args.engine == 'cef':
        kw['cwd'] = str(executable.parent)
    if os.name == 'posix':
        kw['start_new_session'] = True
    proc = subprocess.Popen(command, **kw)
    try:
        output, _ = proc.communicate(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        if os.name == 'posix':
            os.killpg(proc.pid, signal.SIGKILL)
        else:
            subprocess.run(['taskkill', '/F', '/T', '/PID', str(proc.pid)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           check=False)
        output, _ = proc.communicate()
        print(output[-6000:])
        raise RuntimeError('Browser benchmark timed out')
    records = [line.split('xytron-benchmark-json:', 1)[1]
               for line in output.splitlines()
               if line.startswith('xytron-benchmark-json:')]
    if proc.returncode != 0 or len(records) != 1:
        print(output[-12000:])
        raise RuntimeError('Browser benchmark failed: code=' +
                           str(proc.returncode) + ', records=' + str(len(records)))
    data = json.loads(records[0])
    if data.get('schema') != 1 or len(data.get('records', [])) < 8:
        raise RuntimeError('Invalid or incomplete benchmark record')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    metadata = dict(engine=args.engine, os=platform.system(),
                    cpu=platform.machine(), runner_os=os.getenv('RUNNER_OS', ''),
                    runner_arch=os.getenv('RUNNER_ARCH', ''),
                    runner_image=os.getenv('ImageOS', ''),
                    github_sha=os.getenv('GITHUB_SHA', ''),
                    python=platform.python_version(),
                    timestamp_utc=datetime.now(timezone.utc).isoformat(),
                    warmup=args.warmup, samples_requested=args.samples)
    summary = []
    for row in data['records']:
        values = row['samples_ms']
        if not values or not all(isinstance(x, (int, float)) and
                                 math.isfinite(x) and x >= 0 for x in values):
            raise RuntimeError('Invalid sample data: ' + str(row.get('id')))
        count = len(values)
        elapsed = row.get('elapsed_ms', sum(values))
        record = dict(id=row['id'], count=count, concurrency=row['concurrency'],
                      p50_ms=percentile(values, .5), p95_ms=percentile(values, .95),
                      p99_ms=percentile(values, .99), mean_ms=statistics.mean(values),
                      min_ms=min(values), max_ms=max(values),
                      stddev_ms=statistics.pstdev(values), elapsed_ms=elapsed,
                      quantized_zero_fraction=values.count(0)/count,
                      ops_per_second=row.get('throughput_ops_s',
                                             count*1000/elapsed if elapsed else None))
        summary.append(record)
    prefix = args.engine + '-' + platform.machine()
    with (args.output_dir / (prefix + '-raw.json')).open('w', encoding='utf8') as f:
        json.dump({'metadata':metadata, 'data':data}, f, indent=2)
    with (args.output_dir / (prefix + '-summary.json')).open('w', encoding='utf8') as f:
        json.dump({'metadata':metadata, 'results':summary}, f, indent=2)
    with (args.output_dir / (prefix + '-samples.csv')).open('w', newline='', encoding='utf8') as f:
        csvwriter = csv.writer(f)
        csvwriter.writerow(['id','index','latency_ms'])
        for row in data['records']:
            for i, latency in enumerate(row['samples_ms']):
                csvwriter.writerow([row['id'], i, latency])
    resolution = data.get('clock_resolution_ms')
    if resolution is not None:
        metadata['js_clock_resolution_ms'] = resolution
    lines = ['# Xytron bridge latency: ' + prefix,
             '',
             'Diagnostic GitHub runner result; not a performance SLA.',
             ('JS timer minimum observed step: %.4f ms.' % resolution
              if resolution is not None else 'JS timer resolution unknown.'),
             'A 0-ms sample is timer quantization, NOT zero transport latency.',
             'Percentiles below timer resolution must not be treated as precise.',
             '',
             '',
             'Commit: ' + (metadata['github_sha'] or 'local'),
             '',
             '| Scenario | N | p50 ms | p95 ms | p99 ms | ops/s |',
             '| --- | ---: | ---: | ---: | ---: | ---: |']
    for row in summary:
        ops = ('%.1f' % row['ops_per_second']
               if row['ops_per_second'] is not None else '—')
        lines.append('| %s | %d | %.3f | %.3f | %.3f | %s |' %
                     (row['id'],row['count'],row['p50_ms'],row['p95_ms'],
                      row['p99_ms'],ops))
    (args.output_dir / (prefix + '-summary.md')).write_text(
        '\n'.join(lines)+'\n', encoding='utf8')
    print('\n'.join(lines))
    print('Benchmark artifacts:', args.output_dir)


if __name__ == '__main__':
    try:
        run()
    except (RuntimeError, ValueError, OSError, KeyError) as error:
        print('Bridge benchmark error:', error, file=sys.stderr)
        sys.exit(1)
