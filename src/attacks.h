#pragma once
/**
 * @file attacks.h
 * @brief Attack helpers for sliding and leaper pieces.
 *
 * The current implementation computes slider rays on the fly and caches
 * leaper attacks in simple lookup tables. `init_attacks` is kept as a hook so
 * future CPU-specific dispatch can drop in without disturbing call sites.
 */

#include "common.h"

namespace bby {

void init_attacks(bool use_pext);

Bitboard rook_attacks(Square sq, Bitboard occ);
Bitboard bishop_attacks(Square sq, Bitboard occ);
Bitboard knight_attacks(Square sq);
Bitboard king_attacks(Square sq);
Bitboard pawn_attacks(Color color, Square sq);

}  // namespace bby
