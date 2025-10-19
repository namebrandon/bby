# Brilliant, But Why?

A modern, production-focused chess engine engineered for deterministic performance, rapid diagnostics, and strong cross-platform tooling. This repository houses the full engine, supporting utilities, and test assets.

## Getting Started

```bash
# configure and build the debug profile (artifacts land in build/<profile>/)
make debug

# run the engine (UCI protocol)
./build/debug/bby
# inside the engine, try `bench` to run the deterministic 50-position suite
# debug helpers: `trace status`, `assert`, `repropack`
```

See `docs/` and `external/docs/bby_project.md` for the full development plan, quality bar, and module responsibilities. Licensing follows the GNU General Public License v3.0 (`COPYING`).

## Build Profiles

All primary build profiles funnel through CMake and emit binaries in `build/<profile>/`:

- `make debug` – `-O0 -g -DGLOBAL_DEBUG_LEVEL=2`
- `make release` – `-O3 -DNDEBUG`
- `make profile` – `-O3 -pg -DNDEBUG`
- `make sanitize` – RelWithDebInfo with AddressSanitizer + UndefinedBehaviorSanitizer
- `make tsan` – RelWithDebInfo with ThreadSanitizer

Utility targets:

- `make perft` – build the perft oracle in `tools/`
- `make sanitize-test` – build + run unit tests under ASan/UBSan
- `make tsan-test` – build + run unit tests under TSan
- `make clean` – remove the `build/` tree only (preserves caches/packages)

### Sanitizer Workflows

The sanitizer targets configure dedicated build trees with the correct instrumentation:

```bash
# AddressSanitizer + UndefinedBehaviorSanitizer
make sanitize-test

# ThreadSanitizer (slow; keep thread count low)
make tsan-test
```

The commands compile the engine and unit tests with the chosen sanitizer, then execute the Catch2 suite via `ctest --output-on-failure`.

### Tests

Catch2 3.x is fetched automatically by CMake. After configuring any build tree:

```bash
cmake --build build/debug --target bby-unit
ctest --test-dir build/debug
```

Quick perft probe:

```bash
./out/debug/bby-perft --depth 4 --fen "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
```

### Manual QA Checklist

Run these locally before large merges or tagging a release:

- `ctest --test-dir build/debug --output-on-failure`
- `make sanitize-test` (Address/UB sanitizers)
- `make tsan-test` (ThreadSanitizer; slower, run when touching threading code)

## Repository Layout (summary)

```
bby/
├─ README.md
├─ COPYING
├─ CMakeLists.txt
├─ Makefile
├─ logos/
├─ src/              # engine modules (one pair header/impl per concept)
├─ test/             # unit, fuzz, and perft suites
├─ tools/            # developer utilities and harnesses
└─ external/         # mounted volume (ignored by git)
```

Refer to `external/docs/bby_project.md` for authoritative module contracts, coding guidelines, and Definition of Done.
