#pragma once
// bench.h -- Deterministic bench suite positions for the UCI `bench` command.
// Contains 50 FEN strings used to measure baseline move generation throughput.

#include <array>
#include <string_view>

namespace bby {

inline constexpr std::array<std::string_view, 50> kBenchFens = {
    // Base set of 10 positions repeated to reach 50 entries.
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/2P5/1p2P3/2N2Q1p/PPPB1PPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "rnbq1bnr/ppppkppp/8/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQ - 2 4",
    "r4rk1/1pp1qppp/p1np1n2/4p3/1PP1P3/P1N1BP2/3P2PP/R2Q1RK1 w - - 0 1",
    "r2q1rk1/pp2bppp/2n1pn2/2pp4/3P4/1P1BPN2/PB3PPP/R2Q1RK1 w - - 0 1",
    "4rrk1/1pp1qppp/p1np1n2/4p3/1PP1P3/P1N1BP2/3P2PP/R2QR1K1 w - - 0 1",
    "rnbqkbnr/pp1ppppp/2p5/8/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2",
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R w KQkq - 0 6",

    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/2P5/1p2P3/2N2Q1p/PPPB1PPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "rnbq1bnr/ppppkppp/8/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQ - 2 4",
    "r4rk1/1pp1qppp/p1np1n2/4p3/1PP1P3/P1N1BP2/3P2PP/R2Q1RK1 w - - 0 1",
    "r2q1rk1/pp2bppp/2n1pn2/2pp4/3P4/1P1BPN2/PB3PPP/R2Q1RK1 w - - 0 1",
    "4rrk1/1pp1qppp/p1np1n2/4p3/1PP1P3/P1N1BP2/3P2PP/R2QR1K1 w - - 0 1",
    "rnbqkbnr/pp1ppppp/2p5/8/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2",
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R w KQkq - 0 6",

    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/2P5/1p2P3/2N2Q1p/PPPB1PPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "rnbq1bnr/ppppkppp/8/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQ - 2 4",
    "r4rk1/1pp1qppp/p1np1n2/4p3/1PP1P3/P1N1BP2/3P2PP/R2Q1RK1 w - - 0 1",
    "r2q1rk1/pp2bppp/2n1pn2/2pp4/3P4/1P1BPN2/PB3PPP/R2Q1RK1 w - - 0 1",
    "4rrk1/1pp1qppp/p1np1n2/4p3/1PP1P3/P1N1BP2/3P2PP/R2QR1K1 w - - 0 1",
    "rnbqkbnr/pp1ppppp/2p5/8/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2",
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R w KQkq - 0 6",

    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/2P5/1p2P3/2N2Q1p/PPPB1PPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "rnbq1bnr/ppppkppp/8/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQ - 2 4",
    "r4rk1/1pp1qppp/p1np1n2/4p3/1PP1P3/P1N1BP2/3P2PP/R2Q1RK1 w - - 0 1",
    "r2q1rk1/pp2bppp/2n1pn2/2pp4/3P4/1P1BPN2/PB3PPP/R2Q1RK1 w - - 0 1",
    "4rrk1/1pp1qppp/p1np1n2/4p3/1PP1P3/P1N1BP2/3P2PP/R2QR1K1 w - - 0 1",
    "rnbqkbnr/pp1ppppp/2p5/8/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2",
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R w KQkq - 0 6",

    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/2P5/1p2P3/2N2Q1p/PPPB1PPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "rnbq1bnr/ppppkppp/8/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQ - 2 4",
    "r4rk1/1pp1qppp/p1np1n2/4p3/1PP1P3/P1N1BP2/3P2PP/R2Q1RK1 w - - 0 1",
    "r2q1rk1/pp2bppp/2n1pn2/2pp4/3P4/1P1BPN2/PB3PPP/R2Q1RK1 w - - 0 1",
    "4rrk1/1pp1qppp/p1np1n2/4p3/1PP1P3/P1N1BP2/3P2PP/R2QR1K1 w - - 0 1",
    "rnbqkbnr/pp1ppppp/2p5/8/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2",
    "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R w KQkq - 0 6"};

static_assert(kBenchFens.size() == 50, "Bench suite must contain 50 positions");

}  // namespace bby
