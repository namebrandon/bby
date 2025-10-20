#include "eval.h"

#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

namespace bby::test {

namespace {

std::string mirror_and_swap_fen(std::string_view fen, bool flip_side_to_move) {
  std::array<std::string_view, 6> fields{};
  std::size_t cursor = 0;
  for (std::size_t idx = 0; idx < fields.size(); ++idx) {
    const std::size_t next = fen.find(' ', cursor);
    if (next == std::string_view::npos) {
      fields[idx] = fen.substr(cursor);
      cursor = fen.size();
    } else {
      fields[idx] = fen.substr(cursor, next - cursor);
      cursor = next + 1;
    }
  }

  std::array<char, 64> squares{};
  squares.fill('.');
  int rank = 7;
  int file = 0;
  for (char ch : fields[0]) {
    if (ch == '/') {
      --rank;
      file = 0;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      const int empty = ch - '0';
      for (int i = 0; i < empty; ++i) {
        squares[rank * 8 + file++] = '.';
      }
    } else {
      squares[rank * 8 + file++] = ch;
    }
  }

  std::array<char, 64> mirrored{};
  mirrored.fill('.');
  for (int idx = 0; idx < 64; ++idx) {
    const char piece = squares[idx];
    if (piece == '.') {
      continue;
    }
    const int mirror_idx = idx ^ 56;
    const char swapped =
        std::islower(static_cast<unsigned char>(piece))
            ? static_cast<char>(std::toupper(static_cast<unsigned char>(piece)))
            : static_cast<char>(std::tolower(static_cast<unsigned char>(piece)));
    mirrored[mirror_idx] = swapped;
  }

  std::ostringstream board;
  for (int r = 7; r >= 0; --r) {
    int empty = 0;
    for (int f = 0; f < 8; ++f) {
      const char piece = mirrored[r * 8 + f];
      if (piece == '.') {
        ++empty;
      } else {
        if (empty != 0) {
          board << empty;
          empty = 0;
        }
        board << piece;
      }
    }
    if (empty != 0) {
      board << empty;
    }
    if (r != 0) {
      board << '/';
    }
  }

  const char original_side =
      fields[1].empty() ? 'w' : static_cast<char>(fields[1].front());
  const char stm = flip_side_to_move
                       ? (original_side == 'w' ? 'b' : 'w')
                       : original_side;

  std::string castling(fields[2]);
  if (castling != "-") {
    for (char& c : castling) {
      if (std::isalpha(static_cast<unsigned char>(c))) {
        c = std::islower(static_cast<unsigned char>(c))
                ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
    }
  }

  std::string ep(fields[3]);
  if (ep.size() == 2 && ep != "-") {
    const char file_char = ep[0];
    const char rank_char = ep[1];
    ep[0] = static_cast<char>('h' - (file_char - 'a'));
    ep[1] = static_cast<char>('9' - rank_char);
  }

  std::ostringstream out;
  out << board.str() << ' ' << stm << ' ' << castling << ' ' << ep << ' '
      << fields[4] << ' ' << fields[5];
  return out.str();
}

}  // namespace

TEST_CASE("PeSTO evaluation balances start position", "[eval]") {
  constexpr std::string_view start_fen =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  auto pos = Position::from_fen(start_fen, false);

  EvalTrace trace{};
  const auto score = evaluate(pos, &trace);

  REQUIRE(score == 0);
  REQUIRE(trace.midgame == 0);
  REQUIRE(trace.endgame == 0);
  REQUIRE(trace.blended == score);
  REQUIRE(trace.phase == 24);
}

TEST_CASE("Side to move flips tapered score sign", "[eval]") {
  constexpr std::string_view white_to_move_fen =
      "4k3/8/8/8/8/8/3N4/4K3 w - - 0 1";
  auto pos_white = Position::from_fen(white_to_move_fen, false);
  EvalTrace trace_white{};
  const auto score_white = evaluate(pos_white, &trace_white);

  REQUIRE(score_white > 0);
  REQUIRE(trace_white.blended == score_white);
  REQUIRE(trace_white.phase > 0);

  constexpr std::string_view black_to_move_fen =
      "4k3/8/8/8/8/8/3N4/4K3 b - - 0 1";
  auto pos_black = Position::from_fen(black_to_move_fen, false);
  EvalTrace trace_black{};
  const auto score_black = evaluate(pos_black, &trace_black);

  REQUIRE(score_black < 0);
  REQUIRE(score_black == -score_white);
  REQUIRE(trace_black.phase == trace_white.phase);
  REQUIRE(trace_black.blended == score_black);
}

TEST_CASE("Color-flipped mirror negates evaluation", "[eval]") {
  constexpr std::string_view original_fen =
      "4k3/8/8/8/8/8/3N4/4K3 w - - 0 1";

  const auto original = Position::from_fen(original_fen, false);
  const std::string mirrored_fen = mirror_and_swap_fen(original_fen, false);
  const auto mirrored = Position::from_fen(mirrored_fen, false);

  EvalTrace original_trace{};
  EvalTrace mirrored_trace{};
  const auto original_score = evaluate(original, &original_trace);
  const auto mirrored_score = evaluate(mirrored, &mirrored_trace);

  CAPTURE(original_trace.midgame);
  CAPTURE(mirrored_trace.midgame);
  CAPTURE(original_trace.endgame);
  CAPTURE(mirrored_trace.endgame);
  CAPTURE(original_trace.phase);
  CAPTURE(mirrored_trace.phase);

  REQUIRE(mirrored_score == -original_score);
}

}  // namespace bby::test
