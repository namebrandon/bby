#!/usr/bin/env python3
"""Run WAC tactical suite against the current bby binary.

This utility feeds the engine a subset or the entirety of
``tests/positions/wacnew.epd`` and compares bestmoves against the
expected SAN annotations. SAN conversion is handled by the
chess-library helper via the ``bby-san-to-uci`` tool.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional, Sequence, Set, Tuple

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ENGINE = REPO_ROOT / "build" / "release" / "bby"
DEFAULT_CONVERTER = REPO_ROOT / "build" / "release" / "bby-san-to-uci"
DEFAULT_EPD = REPO_ROOT / "tests" / "positions" / "wacnew.epd"


@dataclass
class Position:
    fen: str
    sans: Sequence[str]
    ident: str


def parse_epd(epd_path: Path, limit: Optional[int]) -> List[Position]:
    positions: List[Position] = []
    with epd_path.open("r", encoding="utf-8") as handle:
        for line in handle:
            raw = line.strip()
            if not raw or raw.startswith("#"):
                continue
            bm_idx = raw.find(" bm ")
            if bm_idx == -1:
                continue
            fen = raw[:bm_idx].strip()
            rhs = raw[bm_idx + 4 :]
            semi = rhs.find(";")
            if semi == -1:
                continue
            san_field = rhs[:semi].strip()
            sans = [token for token in san_field.replace(",", " ").split() if token]
            if not sans:
                continue
            ident = ""
            id_marker = rhs.find('id "')
            if id_marker != -1:
                end = rhs.find('"', id_marker + 4)
                if end != -1:
                    ident = rhs[id_marker + 4 : end]
            positions.append(Position(fen=fen, sans=sans, ident=ident))
            if limit is not None and len(positions) >= limit:
                break
    return positions


class Engine:
    def __init__(self, path: Path) -> None:
        self.proc = subprocess.Popen(
            [str(path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

    def _write(self, command: str) -> None:
        assert self.proc.stdin is not None
        self.proc.stdin.write(command + "\n")
        self.proc.stdin.flush()

    def _read_until(self, token: str) -> None:
        assert self.proc.stdout is not None
        for line in self.proc.stdout:
            if token in line:
                return

    def initialise(self) -> None:
        self._write("uci")
        self._read_until("uciok")
        self._write("isready")
        self._read_until("readyok")

    def bestmove(self, fen: str, depth: int) -> str:
        self._write("ucinewgame")
        self._write(f"position fen {fen}")
        self._write(f"go depth {depth}")
        assert self.proc.stdout is not None
        for line in self.proc.stdout:
            if line.startswith("bestmove"):
                parts = line.split()
                if len(parts) >= 2:
                    return parts[1]
                return ""
        return ""

    def quit(self) -> None:
        try:
            self._write("quit")
        except BrokenPipeError:
            pass
        self.proc.wait(timeout=5)


def convert_san(converter: Path, fen: str, san: str) -> Optional[str]:
    completed = subprocess.run(
        [str(converter)],
        input=f"{fen}\n{san}\n",
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        sys.stderr.write(f"[san-to-uci] {san} failed: {completed.stderr.strip()}\n")
        return None
    return completed.stdout.strip()


def run_suite(
    engine_path: Path,
    converter_path: Path,
    positions: Sequence[Position],
    depth: int,
    verbose: bool,
    fail_on_miss: bool,
) -> int:
    engine = Engine(engine_path)
    try:
        engine.initialise()
        solved = 0
        total = len(positions)
        for idx, pos in enumerate(positions, start=1):
            expected: Set[str] = set()
            for san in pos.sans:
                converted = convert_san(converter_path, pos.fen, san)
                if converted:
                    expected.add(converted)
            best = engine.bestmove(pos.fen, depth)
            ident = pos.ident or f"WAC.{idx:03d}"
            ok = best in expected and best != ""
            if ok:
                solved += 1
            if verbose or not ok:
                print(
                    f"{ident}: {'OK' if ok else 'MISS'} expected={sorted(expected) if expected else None} got={best}"
                )
        print(f"Solved {solved} / {total} at depth {depth}")
        if fail_on_miss and solved != total:
            return 1
        return 0
    finally:
        engine.quit()


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Run WAC tactical checks against bby")
    parser.add_argument(
        "--mode",
        choices=("quick", "full"),
        default="quick",
        help="quick: first 10 positions, full: entire wacnew.epd (default: quick)",
    )
    parser.add_argument(
        "--depth",
        type=int,
        help="search depth to use (default: 3 for quick, 6 for full)",
    )
    parser.add_argument("--engine", type=Path, default=DEFAULT_ENGINE, help="path to bby binary")
    parser.add_argument(
        "--converter", type=Path, default=DEFAULT_CONVERTER, help="path to bby-san-to-uci helper"
    )
    parser.add_argument("--epd", type=Path, default=DEFAULT_EPD, help="EPD suite path")
    parser.add_argument("--limit", type=int, default=None, help="Cap positions (overrides --mode)")
    parser.add_argument("--verbose", action="store_true", help="Log every position, not just misses")
    parser.add_argument("--fail-on-miss", action="store_true", help="Exit with code 1 if any position fails")
    args = parser.parse_args(argv)

    if not args.engine.exists():
        parser.error(f"engine not found: {args.engine}")
    if not args.converter.exists():
        parser.error(f"converter not found: {args.converter}")
    if not args.epd.exists():
        parser.error(f"EPD file not found: {args.epd}")

    mode_limit = 10 if args.mode == "quick" else None
    limit = args.limit if args.limit is not None else mode_limit
    depth = args.depth if args.depth is not None else (3 if args.mode == "quick" else 6)

    positions = parse_epd(args.epd, limit)
    if not positions:
        parser.error("no positions parsed from EPD")

    return run_suite(args.engine, args.converter, positions, depth, args.verbose, args.fail_on_miss)


if __name__ == "__main__":
    sys.exit(main())
