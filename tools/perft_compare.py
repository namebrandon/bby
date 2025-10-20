#!/usr/bin/env python3
"""Run perft on BBY and a reference engine, highlighting mismatches."""

import argparse
import re
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List


@dataclass
class Reference:
    fen: str
    depth: int
    nodes: int


@dataclass
class Sample:
    nodes: int
    millis: int


def load_suite(path: Path) -> List[Reference]:
    refs: List[Reference] = []
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fen, depth, nodes = line.split("|")
            refs.append(Reference(fen=fen, depth=int(depth), nodes=int(nodes)))
    return refs


def run_bby_perft(binary: Path, fen: str, depth: int, timeout: float) -> Sample:
    cmd = [str(binary), "--fen", fen, "--depth", str(depth)]
    start = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    elapsed = int((time.perf_counter() - start) * 1000.0)
    if proc.returncode != 0:
        raise RuntimeError(f"bby-perft failed: {proc.stderr}")
    match = re.search(r"nodes=(\d+)", proc.stdout)
    if not match:
        raise RuntimeError(f"Unable to parse nodes from bby output:\n{proc.stdout}")
    nodes = int(match.group(1))
    return Sample(nodes=nodes, millis=elapsed)


def run_engine_perft(engine: Path, fen: str, depth: int, timeout: float) -> Sample:
    commands = f"uci\nsetoption name Threads value 1\nposition fen {fen}\ngo perft {depth}\nquit\n"
    start = time.perf_counter()
    proc = subprocess.run([str(engine)], input=commands, capture_output=True,
                          text=True, timeout=timeout)
    elapsed = int((time.perf_counter() - start) * 1000.0)
    if proc.returncode != 0:
        raise RuntimeError(f"Engine {engine} failed: {proc.stderr}")
    nodes = None
    for line in reversed(proc.stdout.splitlines()):
        if "Nodes searched:" in line:
            try:
                nodes = int(line.split(":", 1)[1].strip())
            except ValueError:
                pass
            break
    if nodes is None:
        raise RuntimeError(f"Could not find node summary in engine output:\n{proc.stdout}")
    return Sample(nodes=nodes, millis=elapsed)


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare BBY perft against a reference engine")
    parser.add_argument("engine", help="Path to reference UCI engine")
    parser.add_argument("suite", help="Suite file (fen|depth|expected)")
    parser.add_argument("--bby", default="build/release/bby-perft",
                        help="Path to bby-perft executable (default: build/release/bby-perft)")
    parser.add_argument("--depth", type=int, default=None,
                        help="Optional depth cap; runs min(ref_depth, cap)")
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="Per-position timeout in seconds (default 120)")
    args = parser.parse_args()

    suite = load_suite(Path(args.suite))
    bby_bin = Path(args.bby)
    engine_bin = Path(args.engine)

    header = f"{'#':<3}{'FEN':<35}{'dep':<4}{'BBY nodes':>14}{'BBY ms':>10}{'Ref nodes':>14}{'Ref ms':>10}{'status':>10}"
    print(header)
    print("-" * len(header))

    all_ok = True
    for idx, ref in enumerate(suite, start=1):
        depth = ref.depth if args.depth is None else min(ref.depth, args.depth)
        bby_sample = run_bby_perft(bby_bin, ref.fen, depth, args.timeout)
        ref_sample = run_engine_perft(engine_bin, ref.fen, depth, args.timeout)

        status_bits = []
        if bby_sample.nodes == ref_sample.nodes:
            status_bits.append("eq")
        else:
            status_bits.append("diff")
            all_ok = False
        if depth == ref.depth and ref.nodes:
            if bby_sample.nodes == ref.nodes:
                status_bits.append("ref")
            else:
                status_bits.append("refdiff")
                all_ok = False

        print(f"{idx:<3}{ref.fen:<35}{depth:<4}{bby_sample.nodes:>14}{bby_sample.millis:>10}"
              f"{ref_sample.nodes:>14}{ref_sample.millis:>10}{','.join(status_bits):>10}")

    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
