#include "pgn.h"

#include <cctype>
#include <sstream>
#include <string_view>
#include <utility>

namespace bby {
namespace {

bool is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view trim_view(std::string_view sv) {
  while (!sv.empty() && is_whitespace(sv.front())) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && is_whitespace(sv.back())) {
    sv.remove_suffix(1);
  }
  return sv;
}

std::string trim_copy(std::string_view sv) {
  return std::string(trim_view(sv));
}

bool parse_tag_line(std::string_view line, std::string& key, std::string& value) {
  line = trim_view(line);
  if (line.size() < 4 || line.front() != '[' || line.back() != ']') {
    return false;
  }
  line.remove_prefix(1);
  line.remove_suffix(1);
  const auto first_space = line.find_first_of(" \t");
  if (first_space == std::string_view::npos) {
    return false;
  }
  key = std::string(trim_copy(line.substr(0, first_space)));
  const auto quote_begin = line.find('"', first_space);
  if (quote_begin == std::string_view::npos) {
    return false;
  }
  const auto quote_end = line.find('"', quote_begin + 1);
  if (quote_end == std::string_view::npos) {
    return false;
  }
  value = std::string(line.substr(quote_begin + 1, quote_end - (quote_begin + 1)));
  return true;
}

bool is_result_token(std::string_view token) {
  return token == "1-0" || token == "0-1" || token == "1/2-1/2" || token == "*";
}

std::string sanitize_token(std::string token) {
  if (token.empty()) {
    return token;
  }

  // Remove leading digits, dots, and ellipsis markers.
  std::size_t idx = 0;
  while (idx < token.size() &&
         (std::isdigit(static_cast<unsigned char>(token[idx])) ||
          token[idx] == '.' || token[idx] == '!')) {
    ++idx;
  }
  token.erase(0, idx);
  while (!token.empty() && token.front() == '.') {
    token.erase(token.begin());
  }
  while (!token.empty() && token.front() == '+') {
    token.erase(token.begin());
  }

  // Skip numeric annotation glyphs.
  if (!token.empty() && token.front() == '$') {
    return {};
  }

  return token;
}

void flush_token(std::string& token,
                 std::string& pending_comment,
                 PgnGame& game) {
  const std::string trimmed = trim_copy(token);
  token.clear();
  if (trimmed.empty()) {
    return;
  }
  if (is_result_token(trimmed)) {
    game.result = trimmed;
    return;
  }

  std::string san = sanitize_token(trimmed);
  if (san.empty()) {
    return;
  }

  PgnMove move;
  move.san = std::move(san);
  if (!pending_comment.empty()) {
    move.comment = std::move(pending_comment);
    pending_comment.clear();
  }
  game.moves.push_back(std::move(move));
}

void parse_moves_block(const std::string& block, PgnGame& game) {
  std::string token;
  std::string comment_buffer;
  std::string pending_comment;
  bool in_comment = false;
  bool line_comment = false;
  int variation_depth = 0;

  auto finish_line_comment = [&]() {
    line_comment = false;
  };

  for (char ch : block) {
    if (line_comment) {
      if (ch == '\n') {
        finish_line_comment();
      }
      continue;
    }

    if (in_comment) {
      if (ch == '}') {
        in_comment = false;
        pending_comment = trim_copy(comment_buffer);
        comment_buffer.clear();
      } else {
        comment_buffer.push_back(ch);
      }
      continue;
    }

    if (ch == '{') {
      in_comment = true;
      comment_buffer.clear();
      continue;
    }
    if (ch == ';') {
      line_comment = true;
      continue;
    }
    if (ch == '(') {
      ++variation_depth;
      continue;
    }
    if (ch == ')') {
      if (variation_depth > 0) {
        --variation_depth;
      }
      continue;
    }
    if (variation_depth > 0) {
      continue;
    }

    if (is_whitespace(ch)) {
      flush_token(token, pending_comment, game);
      continue;
    }

    token.push_back(ch);
  }

  flush_token(token, pending_comment, game);
}

}  // namespace

PgnReader::PgnReader(std::istream& input) : input_(input) {}

bool PgnReader::read_line(std::string& out_line) {
  if (have_pending_) {
    out_line = std::move(pending_line_);
    have_pending_ = false;
    return true;
  }
  if (!std::getline(input_, out_line)) {
    return false;
  }
  if (!out_line.empty() && out_line.back() == '\r') {
    out_line.pop_back();
  }
  return true;
}

void PgnReader::push_line(std::string line) {
  pending_line_ = std::move(line);
  have_pending_ = true;
}

bool PgnReader::read_next(PgnGame& out_game, std::string& error) {
  error.clear();
  out_game = PgnGame{};

  std::string line;
  bool have_content = false;

  // Skip leading blank lines.
  while (read_line(line)) {
    if (!trim_copy(line).empty()) {
      have_content = true;
      break;
    }
  }

  if (!have_content) {
    return false;
  }

  if (!line.empty() && line.front() == '[') {
    do {
      std::string key;
      std::string value;
      if (!parse_tag_line(line, key, value)) {
        error = "invalid PGN tag line";
        return false;
      }
      out_game.tags.emplace(std::move(key), std::move(value));
      if (!read_line(line)) {
        line.clear();
        break;
      }
      if (line.empty()) {
        break;
      }
    } while (!line.empty() && line.front() == '[');

    if (!line.empty() && line.front() != '[') {
      push_line(std::move(line));
    }
  } else {
    push_line(std::move(line));
  }

  std::ostringstream moves_blob;
  bool saw_moves = false;
  while (read_line(line)) {
    if (line.empty()) {
      if (saw_moves) {
        break;
      }
      continue;
    }
    if (line.front() == '[') {
      push_line(std::move(line));
      break;
    }
    if (saw_moves) {
      moves_blob << '\n';
    }
    moves_blob << line;
    saw_moves = true;
  }

  const std::string moves_text = moves_blob.str();
  if (moves_text.empty()) {
    error = "no moves section found in PGN game";
    return false;
  }

  parse_moves_block(moves_text, out_game);
  if (out_game.result.empty()) {
    out_game.result = "*";
  }
  if (out_game.moves.empty() && out_game.result == "*") {
    error = "PGN game contains neither moves nor result";
    return false;
  }

  return true;
}

}  // namespace bby
