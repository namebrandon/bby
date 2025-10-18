#pragma once
/**
 * @file pgn.h
 * @brief Streaming Portable Game Notation reader with lightweight tokenization.
 *
 * The reader consumes PGN text incrementally from an input stream and produces
 * `PgnGame` records containing tag pairs, SAN move text, and the reported result.
 * Parsing skips comments, recursive annotation variations, and numeric annotation
 * glyphs; complexity is linear in the input size with a single pass over the data.
 */

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace bby {

struct PgnMove {
  std::string san;
  std::string comment;
};

struct PgnGame {
  std::map<std::string, std::string> tags;
  std::vector<PgnMove> moves;
  std::string result{"*"};
};

class PgnReader {
public:
  explicit PgnReader(std::istream& input);

  bool read_next(PgnGame& out_game, std::string& error);

private:
  bool read_line(std::string& out_line);
  void push_line(std::string line);

  std::istream& input_;
  std::string pending_line_;
  bool have_pending_{false};
};

}  // namespace bby
