#pragma once
/**
 * @file attacks.h
 * @brief Attack helpers for sliding and leaper pieces.
 *
 * Sliding attacks rely on BMI2 PEXT tables when the binary is compiled with
 * BMI2 support; otherwise they use precomputed magic-bitboard lookup tables.
 * The helper `init_attacks` validates configuration requests but does not
 * retain any mutable global state.
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
