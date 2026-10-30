#!/usr/bin/env python3
"""Summarize independent native->JS Base64 vs SharedBuffer same-page probes."""
import argparse
import json
import math
from pathlib import Path
import statistics


def percentile(values, proportion):
    ordered = sorted(values)
    at = (len(ordered)-1)*proportion
    low = math.floor(at)
    high = math.ceil(at)
    return ordered[low]+(ordered[high]-ordered[low])*(at-low)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log',required=True,type=Path)
    parser.add_argument('--output-dir',required=True,type=Path)
    args=parser.parse_args()
    lines=args.log.read_text(encoding='utf-8-sig').splitlines()
    marker='xytron-sharedbuffer-probe-json:'
    records=[json.loads(line.split(marker,1)[1])
             for line in lines if marker in line]
    if len(records)!=1:
        raise ValueError('Expected one successful probe result')
    result=records[0]
    if result.get('release_count')!=53 or len(result.get('samples',[]))!=106:
        raise ValueError('Missing SharedBuffer release or missing sample')
    expected={1024:25,65536:20,1048576:8}
    summary=[]
    lines=['# Same-document Native->JS transport probe',
           '',
           'JS control event -> native alloc/fill -> JS receipt; validation excluded from timing.',
           'SharedBuffer bytes are copied into shared memory once and explicitly released.',
           'These are NOT JS->Native->JS Binary RPC timings.',
           '',
           '| Bytes | Transport | N | p50 ms | p95 ms |',
           '| ---: | --- | ---: | ---: | ---: |']
    for size,count in expected.items():
        for kind in ('base64','shared'):
            timings=[s['elapsed_ms'] for s in result['samples']
                     if s['size']==size and s['transport']==kind]
            if len(timings)!=count or any(not isinstance(x,(int,float))
                   or not math.isfinite(x) or x<0 for x in timings):
                raise ValueError('Invalid sample: %s %d' % (kind,size))
            row={'size_bytes':size,'transport':kind,'count':count,
                 'p50_ms':percentile(timings,.5),
                 'p95_ms':percentile(timings,.95),
                 'mean_ms':statistics.mean(timings)}
            summary.append(row)
            lines.append('| %d | %s | %d | %.3f | %.3f |' % (
                size,kind,count,row['p50_ms'],row['p95_ms']))
    args.output_dir.mkdir(parents=True,exist_ok=True)
    (args.output_dir/'sharedbuffer-comparison.json').write_text(
        json.dumps({'raw':result,'summary':summary},indent=2)+'\n',encoding='utf8')
    (args.output_dir/'sharedbuffer-comparison.md').write_text(
        '\n'.join(lines)+'\n',encoding='utf8')
    print('\n'.join(lines))


if __name__=='__main__':
    main()
