#include "attacks.h"

#include <array>
#include <bit>
#include <cstdint>
#include <vector>

#if defined(__BMI2__) || defined(_MSC_VER)
#include <immintrin.h>
#endif

namespace bby {
namespace {

constexpr Bitboard kFileA = 0x0101010101010101ULL;
constexpr Bitboard kFileH = 0x8080808080808080ULL;

constexpr Bitboard north(Bitboard bb) { return bb << 8; }
constexpr Bitboard south(Bitboard bb) { return bb >> 8; }
constexpr Bitboard east(Bitboard bb) { return (bb << 1) & ~kFileA; }
constexpr Bitboard west(Bitboard bb) { return (bb >> 1) & ~kFileH; }
constexpr Bitboard north_east(Bitboard bb) { return (bb << 9) & ~kFileA; }
constexpr Bitboard north_west(Bitboard bb) { return (bb << 7) & ~kFileH; }
constexpr Bitboard south_east(Bitboard bb) { return (bb >> 7) & ~kFileA; }
constexpr Bitboard south_west(Bitboard bb) { return (bb >> 9) & ~kFileH; }

constexpr std::array<Bitboard, 64> kKnightAttacks = [] {
  std::array<Bitboard, 64> table{};
  for (int sq = 0; sq < 64; ++sq) {
    Bitboard attacks = 0ULL;
    const int file = sq & 7;
    const int rank = sq >> 3;
    constexpr std::array<std::pair<int, int>, 8> offsets = {{
        {1, 2},  {2, 1},  {2, -1}, {1, -2},
        {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2},
    }};
    for (const auto& [df, dr] : offsets) {
      const int nf = file + df;
      const int nr = rank + dr;
      if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
        attacks |= bit(static_cast<Square>(nr * 8 + nf));
      }
    }
    table[sq] = attacks;
  }
  return table;
}();

constexpr std::array<Bitboard, 64> kKingAttacks = [] {
  std::array<Bitboard, 64> table{};
  for (int sq = 0; sq < 64; ++sq) {
    Bitboard attacks = 0ULL;
    const int file = sq & 7;
    const int rank = sq >> 3;
    for (int df = -1; df <= 1; ++df) {
      for (int dr = -1; dr <= 1; ++dr) {
        if (df == 0 && dr == 0) {
          continue;
        }
        const int nf = file + df;
        const int nr = rank + dr;
        if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
          attacks |= bit(static_cast<Square>(nr * 8 + nf));
        }
      }
    }
    table[sq] = attacks;
  }
  return table;
}();

constexpr std::array<Bitboard, 64> kWhitePawnAttacks = [] {
  std::array<Bitboard, 64> table{};
  for (int sq = 0; sq < 64; ++sq) {
    const Bitboard bb = bit(static_cast<Square>(sq));
    table[sq] = north_east(bb) | north_west(bb);
  }
  return table;
}();

constexpr std::array<Bitboard, 64> kBlackPawnAttacks = [] {
  std::array<Bitboard, 64> table{};
  for (int sq = 0; sq < 64; ++sq) {
    const Bitboard bb = bit(static_cast<Square>(sq));
    table[sq] = south_east(bb) | south_west(bb);
  }
  return table;
}();

Bitboard bishop_attacks_on_the_fly(Square sq, Bitboard occ) {
  Bitboard attacks = 0ULL;
  const int file = static_cast<int>(file_of(sq));
  const int rank = static_cast<int>(rank_of(sq));
  for (int nf = file + 1, nr = rank + 1; nf < 8 && nr < 8; ++nf, ++nr) {
    const Square target = static_cast<Square>(nr * 8 + nf);
    attacks |= bit(target);
    if (occ & bit(target)) break;
  }
  for (int nf = file - 1, nr = rank + 1; nf >= 0 && nr < 8; --nf, ++nr) {
    const Square target = static_cast<Square>(nr * 8 + nf);
    attacks |= bit(target);
    if (occ & bit(target)) break;
  }
  for (int nf = file + 1, nr = rank - 1; nf < 8 && nr >= 0; ++nf, --nr) {
    const Square target = static_cast<Square>(nr * 8 + nf);
    attacks |= bit(target);
    if (occ & bit(target)) break;
  }
  for (int nf = file - 1, nr = rank - 1; nf >= 0 && nr >= 0; --nf, --nr) {
    const Square target = static_cast<Square>(nr * 8 + nf);
    attacks |= bit(target);
    if (occ & bit(target)) break;
  }
  return attacks;
}

Bitboard rook_attacks_on_the_fly(Square sq, Bitboard occ) {
  Bitboard attacks = 0ULL;
  const int file = static_cast<int>(file_of(sq));
  const int rank = static_cast<int>(rank_of(sq));
  for (int nf = file + 1; nf < 8; ++nf) {
    const Square target = static_cast<Square>(rank * 8 + nf);
    attacks |= bit(target);
    if (occ & bit(target)) break;
  }
  for (int nf = file - 1; nf >= 0; --nf) {
    const Square target = static_cast<Square>(rank * 8 + nf);
    attacks |= bit(target);
    if (occ & bit(target)) break;
  }
  for (int nr = rank + 1; nr < 8; ++nr) {
    const Square target = static_cast<Square>(nr * 8 + file);
    attacks |= bit(target);
    if (occ & bit(target)) break;
  }
  for (int nr = rank - 1; nr >= 0; --nr) {
    const Square target = static_cast<Square>(nr * 8 + file);
    attacks |= bit(target);
    if (occ & bit(target)) break;
  }
  return attacks;
}

Bitboard mask_bishop(Square sq) {
  Bitboard mask = 0ULL;
  const int file = static_cast<int>(file_of(sq));
  const int rank = static_cast<int>(rank_of(sq));
  for (int nf = file + 1, nr = rank + 1; nf <= 6 && nr <= 6; ++nf, ++nr) {
    mask |= bit(static_cast<Square>(nr * 8 + nf));
  }
  for (int nf = file - 1, nr = rank + 1; nf >= 1 && nr <= 6; --nf, ++nr) {
    mask |= bit(static_cast<Square>(nr * 8 + nf));
  }
  for (int nf = file + 1, nr = rank - 1; nf <= 6 && nr >= 1; ++nf, --nr) {
    mask |= bit(static_cast<Square>(nr * 8 + nf));
  }
  for (int nf = file - 1, nr = rank - 1; nf >= 1 && nr >= 1; --nf, --nr) {
    mask |= bit(static_cast<Square>(nr * 8 + nf));
  }
  return mask;
}

Bitboard mask_rook(Square sq) {
  Bitboard mask = 0ULL;
  const int file = static_cast<int>(file_of(sq));
  const int rank = static_cast<int>(rank_of(sq));
  for (int nf = file + 1; nf <= 6; ++nf) {
    mask |= bit(static_cast<Square>(rank * 8 + nf));
  }
  for (int nf = file - 1; nf >= 1; --nf) {
    mask |= bit(static_cast<Square>(rank * 8 + nf));
  }
  for (int nr = rank + 1; nr <= 6; ++nr) {
    mask |= bit(static_cast<Square>(nr * 8 + file));
  }
  for (int nr = rank - 1; nr >= 1; --nr) {
    mask |= bit(static_cast<Square>(nr * 8 + file));
  }
  return mask;
}

Bitboard set_occupancy(std::uint64_t index, int bits, Bitboard mask) {
  Bitboard occ = 0ULL;
  for (int i = 0; i < bits; ++i) {
    const Bitboard lsb = mask & -mask;
    mask &= mask - 1;
    if (index & (1ULL << i)) {
      occ |= lsb;
    }
  }
  return occ;
}

inline std::uint64_t soft_pext(std::uint64_t src, std::uint64_t mask) {
  std::uint64_t result = 0;
  std::uint64_t bit = 1;
  while (mask) {
    const std::uint64_t lsb = mask & -mask;
    if (src & lsb) {
      result |= bit;
    }
    mask &= mask - 1;
    bit <<= 1;
  }
  return result;
}

#if defined(__BMI2__) || defined(_MSC_VER)
inline std::uint64_t hardware_pext(std::uint64_t src, std::uint64_t mask) {
  return _pext_u64(src, mask);
}
#endif

struct SlidingTables {
  std::array<Bitboard, 64> bishop_masks{};
  std::array<Bitboard, 64> rook_masks{};
  std::array<std::vector<Bitboard>, 64> bishop_table;
  std::array<std::vector<Bitboard>, 64> rook_table;
};

SlidingTables g_tables;
bool g_use_pext = false;
bool g_ready = false;

void build_tables(bool use_pext) {
  for (int sq = 0; sq < 64; ++sq) {
    const Square square = static_cast<Square>(sq);
    const Bitboard bishop_mask = mask_bishop(square);
    const Bitboard rook_mask = mask_rook(square);
    const int bishop_bits = std::popcount(bishop_mask);
    const int rook_bits = std::popcount(rook_mask);

    g_tables.bishop_masks[sq] = bishop_mask;
    g_tables.rook_masks[sq] = rook_mask;
    if (use_pext) {
      g_tables.bishop_table[sq].assign(1ULL << bishop_bits, 0ULL);
      for (std::uint64_t index = 0; index < (1ULL << bishop_bits); ++index) {
        const Bitboard occ = set_occupancy(index, bishop_bits, bishop_mask);
        const Bitboard attack = bishop_attacks_on_the_fly(square, occ);
        g_tables.bishop_table[sq][index] = attack;
      }

      g_tables.rook_table[sq].assign(1ULL << rook_bits, 0ULL);
      for (std::uint64_t index = 0; index < (1ULL << rook_bits); ++index) {
        const Bitboard occ = set_occupancy(index, rook_bits, rook_mask);
        const Bitboard attack = rook_attacks_on_the_fly(square, occ);
        g_tables.rook_table[sq][index] = attack;
      }
    } else {
      g_tables.bishop_table[sq].clear();
      g_tables.rook_table[sq].clear();
    }
  }
}

void ensure_ready() {
  if (!g_ready) {
    init_attacks(false);
  }
}

}  // namespace

void init_attacks(bool use_pext) {
#if defined(__BMI2__) || defined(_MSC_VER)
  const bool allow_pext = use_pext;
#else
  const bool allow_pext = false;
#endif

  if (g_ready && g_use_pext == allow_pext) {
    return;
  }

  build_tables(allow_pext);
  g_use_pext = allow_pext;
  g_ready = true;
}

Bitboard rook_attacks(Square sq, Bitboard occ) {
  ensure_ready();
  const int idx = static_cast<int>(sq);
  if (!g_use_pext) {
    return rook_attacks_on_the_fly(sq, occ);
  }
  const Bitboard mask = g_tables.rook_masks[idx];
#if defined(__BMI2__) || defined(_MSC_VER)
  const std::uint64_t key = hardware_pext(occ, mask);
#else
  const std::uint64_t key = soft_pext(occ, mask);
#endif
  return g_tables.rook_table[idx][key];
}

Bitboard bishop_attacks(Square sq, Bitboard occ) {
  ensure_ready();
  const int idx = static_cast<int>(sq);
  if (!g_use_pext) {
    return bishop_attacks_on_the_fly(sq, occ);
  }
  const Bitboard mask = g_tables.bishop_masks[idx];
#if defined(__BMI2__) || defined(_MSC_VER)
  const std::uint64_t key = hardware_pext(occ, mask);
#else
  const std::uint64_t key = soft_pext(occ, mask);
#endif
  return g_tables.bishop_table[idx][key];
}

Bitboard knight_attacks(Square sq) {
  return kKnightAttacks[static_cast<int>(sq)];
}

Bitboard king_attacks(Square sq) {
  return kKingAttacks[static_cast<int>(sq)];
}

Bitboard pawn_attacks(Color color, Square sq) {
  return color == Color::White ? kWhitePawnAttacks[static_cast<int>(sq)]
                               : kBlackPawnAttacks[static_cast<int>(sq)];
}

}  // namespace bby
