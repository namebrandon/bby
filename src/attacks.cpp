#include "attacks.h"

#include <array>
#include <bit>
#include <cstdint>

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

#if defined(__BMI2__) || defined(_MSC_VER)
inline std::uint64_t hardware_pext(std::uint64_t src, std::uint64_t mask) {
  return _pext_u64(src, mask);
}

constexpr std::size_t kMaxRookKey = 1ULL << 12;
constexpr std::size_t kMaxBishopKey = 1ULL << 13;

struct PextTables {
  std::array<Bitboard, 64> bishop_masks{};
  std::array<std::uint8_t, 64> bishop_bits{};
  std::array<std::array<Bitboard, kMaxBishopKey>, 64> bishop_moves{};
  std::array<Bitboard, 64> rook_masks{};
  std::array<std::uint8_t, 64> rook_bits{};
  std::array<std::array<Bitboard, kMaxRookKey>, 64> rook_moves{};
};

PextTables build_pext_tables() {
  PextTables tables{};
  for (int sq = 0; sq < 64; ++sq) {
    const Square square = static_cast<Square>(sq);

    const Bitboard bishop_mask = mask_bishop(square);
    const std::uint8_t bishop_bits = static_cast<std::uint8_t>(std::popcount(bishop_mask));
    tables.bishop_masks[sq] = bishop_mask;
    tables.bishop_bits[sq] = bishop_bits;
    const std::uint64_t bishop_limit = 1ULL << bishop_bits;
    for (std::uint64_t idx = 0; idx < bishop_limit; ++idx) {
      const Bitboard occ = set_occupancy(idx, bishop_bits, bishop_mask);
      tables.bishop_moves[sq][idx] = bishop_attacks_on_the_fly(square, occ);
    }

    const Bitboard rook_mask = mask_rook(square);
    const std::uint8_t rook_bits = static_cast<std::uint8_t>(std::popcount(rook_mask));
    tables.rook_masks[sq] = rook_mask;
    tables.rook_bits[sq] = rook_bits;
    const std::uint64_t rook_limit = 1ULL << rook_bits;
    for (std::uint64_t idx = 0; idx < rook_limit; ++idx) {
      const Bitboard occ = set_occupancy(idx, rook_bits, rook_mask);
      tables.rook_moves[sq][idx] = rook_attacks_on_the_fly(square, occ);
    }
  }
  return tables;
}

const PextTables& pext_tables() {
  static const PextTables tables = build_pext_tables();
  return tables;
}
#else

constexpr std::array<int, 64> kBishopRelevantBits = {
    6, 5, 5, 5, 5, 5, 5, 6,
    5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5,
    6, 5, 5, 5, 5, 5, 5, 6};

constexpr std::array<int, 64> kRookRelevantBits = {
    12, 11, 11, 11, 11, 11, 11, 12,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    11, 10, 10, 10, 10, 10, 10, 11,
    12, 11, 11, 11, 11, 11, 11, 12};

constexpr std::array<std::uint64_t, 64> kBishopMagics = {
    0x007fbfbfbfbfbfffULL, 0x0000a060401007fcULL, 0x0001004008020000ULL,
    0x0000806004000000ULL, 0x0000100400000000ULL, 0x000021c100b20000ULL,
    0x0000040041001000ULL, 0x0000020040008000ULL, 0x0000040002008080ULL,
    0x0000020005004040ULL, 0x0000020004220010ULL, 0x0000002002120004ULL,
    0x0000002040422000ULL, 0x0000001002020000ULL, 0x0000000902020000ULL,
    0x0000000402020000ULL, 0x0000004040802000ULL, 0x0000002002081000ULL,
    0x0000002010080800ULL, 0x0000001004000800ULL, 0x0000004040040500ULL,
    0x0000001000200200ULL, 0x0000008040002000ULL, 0x0000006820004000ULL,
    0x0000004000802080ULL, 0x0000002000401000ULL, 0x0000001000200800ULL,
    0x0000000800100100ULL, 0x0000000400080080ULL, 0x0000000200040040ULL,
    0x0000000100020020ULL, 0x0000008040200040ULL, 0x0000004020100080ULL,
    0x0000002008080040ULL, 0x0000001004040020ULL, 0x0000000802020010ULL,
    0x0000000401010008ULL, 0x0000000200808004ULL, 0x00007fffec002000ULL,
    0x0000004020010080ULL, 0x0000004010008080ULL, 0x0000002008004040ULL,
    0x0000001004010020ULL, 0x0000000802008024ULL, 0x000000040100800aULL,
    0x00007fffe8004000ULL, 0x0000002040008040ULL, 0x0000002010004080ULL,
    0x0000002008080040ULL, 0x0000001004010020ULL, 0x0000000802008010ULL,
    0x0000000401008008ULL, 0x00007fffb0002000ULL, 0x0000004020004080ULL,
    0x0000004010002040ULL, 0x0000002008001000ULL, 0x0000001004000800ULL,
    0x0000000802000400ULL, 0x0000000401000200ULL, 0x00003fff80002000ULL,
    0x0000200040008080ULL, 0x0000200020004040ULL, 0x0000100020002020ULL,
    0x0000100010001010ULL, 0x0000080008000808ULL, 0x0000040004000404ULL,
    0x0000020002000202ULL, 0x0000010001000101ULL};

constexpr std::array<std::uint64_t, 64> kRookMagics = {
    0x8a80104000800020ULL, 0x140002000100040ULL, 0x2801880a0017001ULL,
    0x100081001000420ULL, 0x200020010080420ULL, 0x3001c0002010008ULL,
    0x8480008002000100ULL, 0x2080088004402900ULL, 0x800098204000ULL,
    0x2024401000200040ULL, 0x100802000801000ULL, 0x120800800801000ULL,
    0x208808088000400ULL, 0x2802200800400ULL, 0x2200800100020080ULL,
    0x801000060821100ULL, 0x80044006422000ULL, 0x100808020004000ULL,
    0x12108a0010204200ULL, 0x140848010000802ULL, 0x481828014002800ULL,
    0x8094004002004100ULL, 0x4010040010010802ULL, 0x20008806104ULL,
    0x100400080208000ULL, 0x2040002120081000ULL, 0x21200680100081ULL,
    0x20100080080080ULL, 0x2000a00200410ULL, 0x20080800400ULL,
    0x80088400100102ULL, 0x80004600042881ULL, 0x4040008040800020ULL,
    0x440003000200801ULL, 0x4200011004500ULL, 0x188020010100100ULL,
    0x14800401802800ULL, 0x2080040080800200ULL, 0x124080204001001ULL,
    0x200046502000484ULL, 0x480400080088020ULL, 0x1000422010034000ULL,
    0x30200100110040ULL, 0x100021010009ULL, 0x2002080100110004ULL,
    0x202008004008002ULL, 0x20020004010100ULL, 0x2048440040820001ULL,
    0x101002200408200ULL, 0x40802000401080ULL, 0x4008142004410100ULL,
    0x2060820c0120200ULL, 0x1001004080100ULL, 0x20c020080040080ULL,
    0x2935610830022400ULL, 0x44440041009200ULL, 0x280001040802101ULL,
    0x2100190040002085ULL, 0x80c0084100102001ULL, 0x4024081001000421ULL,
    0x20030a0244872ULL, 0x12001008414402ULL, 0x2006104900a0804ULL,
    0x1004081002402ULL};

constexpr std::size_t kMaxMagicRookSize = 1ULL << 12;
constexpr std::size_t kMaxMagicBishopSize = 1ULL << 13;

struct MagicTables {
  std::array<Bitboard, 64> bishop_masks{};
  std::array<std::uint64_t, 64> bishop_magics{};
  std::array<std::uint8_t, 64> bishop_shifts{};
  std::array<std::array<Bitboard, kMaxMagicBishopSize>, 64> bishop_moves{};
  std::array<Bitboard, 64> rook_masks{};
  std::array<std::uint64_t, 64> rook_magics{};
  std::array<std::uint8_t, 64> rook_shifts{};
  std::array<std::array<Bitboard, kMaxMagicRookSize>, 64> rook_moves{};
};

MagicTables build_magic_tables() {
  MagicTables tables{};
  for (int sq = 0; sq < 64; ++sq) {
    const Square square = static_cast<Square>(sq);

    const Bitboard bmask = mask_bishop(square);
    const int brel = kBishopRelevantBits[sq];
    tables.bishop_masks[sq] = bmask;
    tables.bishop_magics[sq] = kBishopMagics[sq];
    tables.bishop_shifts[sq] = static_cast<std::uint8_t>(64 - brel);
    const std::uint64_t bishop_magic = tables.bishop_magics[sq];
    const int bishop_shift = tables.bishop_shifts[sq];
    const std::uint64_t bishop_limit = 1ULL << brel;
    for (std::uint64_t idx = 0; idx < bishop_limit; ++idx) {
      const Bitboard occ = set_occupancy(idx, brel, bmask);
      const std::size_t key = static_cast<std::size_t>((occ * bishop_magic) >> bishop_shift);
      tables.bishop_moves[sq][key] = bishop_attacks_on_the_fly(square, occ);
    }

    const Bitboard rmask = mask_rook(square);
    const int rrel = kRookRelevantBits[sq];
    tables.rook_masks[sq] = rmask;
    tables.rook_magics[sq] = kRookMagics[sq];
    tables.rook_shifts[sq] = static_cast<std::uint8_t>(64 - rrel);
    const std::uint64_t rook_magic = tables.rook_magics[sq];
    const int rook_shift = tables.rook_shifts[sq];
    const std::uint64_t rook_limit = 1ULL << rrel;
    for (std::uint64_t idx = 0; idx < rook_limit; ++idx) {
      const Bitboard occ = set_occupancy(idx, rrel, rmask);
      const std::size_t key = static_cast<std::size_t>((occ * rook_magic) >> rook_shift);
      tables.rook_moves[sq][key] = rook_attacks_on_the_fly(square, occ);
    }
  }
  return tables;
}

const MagicTables& magic_tables() {
  static const MagicTables tables = build_magic_tables();
  return tables;
}
#endif

}  // namespace

void init_attacks(bool use_pext) {
#if defined(__BMI2__) || defined(_MSC_VER)
  (void)use_pext;
#else
  BBY_ASSERT(!use_pext && "BMI2 attacks requested but not available at compile time");
  (void)use_pext;
#endif
}

Bitboard rook_attacks(Square sq, Bitboard occ) {
#if defined(__BMI2__) || defined(_MSC_VER)
  const auto& tables = pext_tables();
  const int idx = static_cast<int>(sq);
  const Bitboard mask = tables.rook_masks[idx];
  const std::uint64_t key = hardware_pext(occ, mask);
  return tables.rook_moves[idx][key];
#else
  const auto& tables = magic_tables();
  const int idx = static_cast<int>(sq);
  const Bitboard mask = tables.rook_masks[idx];
  const std::uint64_t key = ((occ & mask) * tables.rook_magics[idx]) >> tables.rook_shifts[idx];
  return tables.rook_moves[idx][static_cast<std::size_t>(key)];
#endif
}

Bitboard bishop_attacks(Square sq, Bitboard occ) {
#if defined(__BMI2__) || defined(_MSC_VER)
  const auto& tables = pext_tables();
  const int idx = static_cast<int>(sq);
  const Bitboard mask = tables.bishop_masks[idx];
  const std::uint64_t key = hardware_pext(occ, mask);
  return tables.bishop_moves[idx][key];
#else
  const auto& tables = magic_tables();
  const int idx = static_cast<int>(sq);
  const Bitboard mask = tables.bishop_masks[idx];
  const std::uint64_t key = ((occ & mask) * tables.bishop_magics[idx]) >> tables.bishop_shifts[idx];
  return tables.bishop_moves[idx][static_cast<std::size_t>(key)];
#endif
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
