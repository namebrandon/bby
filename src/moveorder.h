#pragma once
// moveorder.h -- Move ordering heuristics and static exchange evaluation.
// Keeps pure helpers that score moves prior to search traversal.

#include <array>
#include <cstdint>
#include <limits>

#include "board.h"
#include "hash.h"

namespace bby {

/**
 * @brief Small direct-mapped cache for per-position SEE results.
 *
 * Entries are keyed by (zobrist, move). Collisions simply overwrite.
 * The cache is cleared by the caller when a new search starts.
 */
struct SeeCache {
  void clear();
  bool probe(std::uint64_t key, Move move, int& out) const;
  void store(std::uint64_t key, Move move, int value);

private:
  struct Entry {
    std::uint64_t key{0};
    Move move{};
    int value{0};
    bool valid{false};
  };
  static constexpr std::size_t kSize = 128;
  static_assert((kSize & (kSize - 1)) == 0, "SeeCache size must be power of two");

  [[nodiscard]] static std::size_t index(std::uint64_t key, Move move);

  std::array<Entry, kSize> entries_{};
};

struct HistoryTable {
  static constexpr std::size_t kStride = 64 * 64;
  std::array<int, 2 * kStride> values{};

  [[nodiscard]] int get(Color color, Move move) const;
  void add(Color color, Move move, int delta);

private:
  [[nodiscard]] static std::size_t index(Color color, Move move);
};

struct OrderingContext {
  const Position* pos{nullptr};
  const TTEntry* tt{nullptr};
  const HistoryTable* history{nullptr};
  SeeCache* see_cache{nullptr};
  std::array<Move, 2> killers{};
  int ply{0};
};

constexpr int kSeeUnknown = std::numeric_limits<int>::min();

void score_moves(MoveList& ml, const OrderingContext& ctx, std::array<int, kMaxMoves>& scores,
                 std::array<int, kMaxMoves>* see_results = nullptr, bool force_see = false);
void select_best_move(MoveList& ml, std::array<int, kMaxMoves>& scores, std::size_t start, std::size_t end);
int see(const Position& pos, Move m);
int capture_margin(const Position& pos, Move m);
int cached_see(const Position& pos, Move move, SeeCache* cache);

}  // namespace bby
