#pragma once
// bench.h -- Deterministic bench suite positions for the UCI `bench` command.
// Contains 50 FEN strings used to measure baseline move generation throughput.

#include <array>
#include <string_view>

namespace bby {

inline constexpr std::array<std::string_view, 12> kBenchFens = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "r1bqk1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
    "r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "8/pp3p1k/2p2q1p/3r1P2/5R2/7P/P1P1QP2/7K b - - 0 1",
    "r1bq1rk1/pp2nppp/4n3/3ppP2/1b1P4/3BP3/PP2N1PP/R1BQNRK1 b - - 1 8",
    "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 w - - 0 1",
    "r2q1rk1/ppp2ppp/2n1bn2/2bpp3/3P4/3QPN2/PPP1BPPP/R1B1K2R w KQ - 0 8",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"};

static_assert(kBenchFens.size() == 12, "Bench suite must contain 12 positions");

}  // namespace bby
