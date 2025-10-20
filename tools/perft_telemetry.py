#!/usr/bin/env python3
"""Capture perft timing metrics for Phase 1 performance gate."""

import argparse
import re
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

STARTPOS = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

@dataclass
class TelemetryCase:
    name: str
    fen: str
    depth: int
    max_time_ms: Optional[int] = None
    min_nps: Optional[int] = None

@dataclass
class Sample:
    nodes: int
    time_ms: int
    nps: int

CASES: List[TelemetryCase] = [
    TelemetryCase(name="startpos-d6", fen=STARTPOS, depth=6, min_nps=20_000_000),
    TelemetryCase(name="startpos-d7", fen=STARTPOS, depth=7, max_time_ms=60_000),
]

LINE_PATTERN = re.compile(r"nodes=(\d+).*time_ms=(\d+).*nps=(\d+)")


def run_case(binary: Path, case: TelemetryCase, timeout: float) -> Sample:
    cmd = [str(binary), "--fen", case.fen, "--depth", str(case.depth)]
    start = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    elapsed_ms = int((time.perf_counter() - start) * 1000.0)
    if proc.returncode != 0:
        raise RuntimeError(f"bby-perft failed: {proc.stderr}")
    match = LINE_PATTERN.search(proc.stdout)
    if not match:
        raise RuntimeError(f"Unable to parse perft output:\n{proc.stdout}")
    nodes, time_ms, nps = map(int, match.groups())
    # fall back to wall clock if reported time is zero (should not happen)
    if time_ms == 0:
        time_ms = elapsed_ms
    return Sample(nodes=nodes, time_ms=time_ms, nps=nps)


def describe(case: TelemetryCase, sample: Sample) -> str:
    status = []
    if case.min_nps is not None:
        status.append("PASS" if sample.nps >= case.min_nps else "FAIL")
    if case.max_time_ms is not None:
        status.append("PASS" if sample.time_ms <= case.max_time_ms else "FAIL")
    if not status:
        status.append("INFO")
    return ",".join(status)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run perft telemetry checks")
    parser.add_argument("--bby", default="build/release/bby-perft",
                        help="Path to bby-perft executable")
    parser.add_argument("--timeout", type=float, default=600.0,
                        help="Per test timeout in seconds (default 600)")
    parser.add_argument("--output", type=Path, default=None,
                        help="Optional file to append telemetry results")
    args = parser.parse_args()

    binary = Path(args.bby)
    lines = []
    header = f"{'case':<14}{'depth':>6}{'nodes':>16}{'time(ms)':>12}{'nps':>12}{'status':>12}"
    print(header)
    print("-" * len(header))
    for case in CASES:
        sample = run_case(binary, case, timeout=args.timeout)
        status = describe(case, sample)
        line = f"{case.name:<14}{case.depth:>6}{sample.nodes:>16}{sample.time_ms:>12}{sample.nps:>12}{status:>12}"
        print(line)
        lines.append(line)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("a", encoding="utf-8") as handle:
            handle.write(time.strftime("%Y-%m-%d %H:%M:%S"))
            handle.write("\n")
            handle.write("\n".join(lines))
            handle.write("\n\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
