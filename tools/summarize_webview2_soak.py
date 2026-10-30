#!/usr/bin/env python3
"""Parse WebView2 group Private Bytes snapshots from a real shared-buffer soak."""
import argparse
import csv
import json
import re
from pathlib import Path

RECORD = re.compile(
    r"xytron-sharedbuffer-soak-sample:\s*"
    r"cycle=(\d+)\s+private_bytes=(\d+)\s+"
    r"renderer_private_bytes=(\d+)\s+processes=(\d+)\s+"
    r"sampled_processes=(\d+)\s+renderers=(\d+)"
)
FINAL = re.compile(r"xytron-sharedbuffer-soak:PASS\s+cycles=(\d+)\s+logical_megabytes=(\d+)(.*)")
KV = re.compile(r"([a-z_]+)=(\d+)")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log",type=Path,required=True)
    parser.add_argument("--output-dir",type=Path,required=True)
    args=parser.parse_args()
    content=args.log.read_text(encoding="utf-8-sig")
    samples=[]
    for match in RECORD.finditer(content):
        cycle,private,renderer,procs,sampled,renderers=map(int,match.groups())
        samples.append(dict(cycle=cycle,private_bytes=private,
            renderer_private_bytes=renderer,process_count=procs,
            sampled_process_count=sampled,renderer_count=renderers))
    end=FINAL.search(content)
    if not end:
        raise ValueError("Missing successful full-process soak summary")
    cycles=int(end.group(1))
    total_mb=int(end.group(2))
    extra={k:int(v) for k,v in KV.findall(end.group(3))}
    if cycles<4 or total_mb!=cycles*32 or len(samples)!=cycles:
        raise ValueError("Incomplete or inconsistent soak sample collection")
    if [r["cycle"] for r in samples]!=list(range(1,cycles+1)):
        raise ValueError("Missing or unordered soak cycles")
    if any(r["renderer_count"]<1 or r["sampled_process_count"]<2
           or r["private_bytes"]<=0 or r["renderer_private_bytes"]<=0
           for r in samples):
        raise ValueError("Missing renderer process memory information")
    start=extra["process_group_private_start_bytes"]
    finish=extra["process_group_private_end_bytes"]
    maximum=extra["process_group_private_peak_bytes"]
    if start<=0 or finish!=samples[-1]["private_bytes"] or maximum<start:
        raise ValueError("Process-group summary contradicts raw samples")
    args.output_dir.mkdir(parents=True,exist_ok=True)
    summary={"cycles":cycles,"logical_megabytes":total_mb,"start_private_bytes":start,
       "end_private_bytes":finish,"peak_private_bytes":maximum,
       "change_private_bytes":finish-start,
       "peak_renderer_private_bytes":max(s["renderer_private_bytes"] for s in samples),
       "min_renderer_count":min(s["renderer_count"] for s in samples),
       "max_process_count":max(s["process_count"] for s in samples),
       "note":"Private Bytes from WebView2 Environment8; excludes crashpad and shared file mappings"}
    (args.output_dir/"process-memory-summary.json").write_text(
        json.dumps({"summary":summary,"samples":samples},indent=2)+"\n",encoding="utf-8")
    with (args.output_dir/"process-memory-per-cycle.csv").open(
            "w",encoding="utf-8",newline="") as output:
        w=csv.DictWriter(output,fieldnames=list(samples[0]))
        w.writeheader();w.writerows(samples)
    lines=["# WebView2 process-group memory-pressure soak","",
        f"Cycles: **{cycles}**, logical transfer: **{total_mb} MiB**.",
        f"Runtime private bytes: start **{start:,}**, end **{finish:,}**, peak **{maximum:,}**.",
        f"Net change: **{finish-start:+,} bytes**.",
        f"Peak renderer private bytes: **{summary['peak_renderer_private_bytes']:,}**.",
        f"Sampled process count, max: **{summary['max_process_count']}**.",
        "",
        "Diagnostic only; no memory-leak threshold is inferred from one CI VM.",
        "WebView2 GetProcessInfos excludes crashpad; all samples reflect processes alive at sampling time.",
        ""]
    (args.output_dir/"process-memory-summary.md").write_text(
        "\n".join(lines),encoding="utf-8")
    print("\n".join(lines))

if __name__=="__main__":
    main()
