#include "epd.h"

#include <array>
#include <cctype>
#include <exception>
#include <fstream>
#include <utility>

namespace bby {
namespace {

bool is_space(char ch) {
  return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

std::string_view trim_view(std::string_view sv) {
  while (!sv.empty() && is_space(sv.front())) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && is_space(sv.back())) {
    sv.remove_suffix(1);
  }
  return sv;
}

std::string trim_copy(std::string_view sv) {
  return std::string(trim_view(sv));
}

std::string_view consume_token(std::string_view& sv) {
  sv = trim_view(sv);
  std::size_t idx = 0;
  while (idx < sv.size() && !is_space(sv[idx])) {
    ++idx;
  }
  std::string_view token = sv.substr(0, idx);
  sv.remove_prefix(idx);
  sv = trim_view(sv);
  return token;
}

bool parse_operations(std::string_view text, std::map<std::string, std::string>& operations,
                      std::string& error) {
  std::string current;
  current.reserve(text.size());
  bool in_quote = false;
  bool escape = false;

  const auto flush_token = [&]() -> bool {
    std::string token = trim_copy(current);
    current.clear();
    if (token.empty()) {
      return true;
    }
    std::size_t split = 0;
    while (split < token.size() && !is_space(token[split])) {
      ++split;
    }
    if (split == 0) {
      error = "EPD operation missing opcode";
      return false;
    }
    std::string opcode = token.substr(0, split);
    BBY_ASSERT(!opcode.empty());
    std::string opcode_for_error = opcode;
    std::string value;
    if (split < token.size()) {
      std::string_view tail(token);
      tail.remove_prefix(split);
      value = trim_copy(tail);
    }
    auto [it, inserted] = operations.emplace(std::move(opcode), std::move(value));
    if (!inserted) {
      error = "Duplicate EPD opcode: " + opcode_for_error;
      return false;
    }
    return true;
  };

  for (char ch : text) {
    if (escape) {
      current.push_back(ch);
      escape = false;
      continue;
    }
    if (ch == '\\') {
      current.push_back(ch);
      escape = true;
      continue;
    }
    if (ch == '"') {
      current.push_back(ch);
      in_quote = !in_quote;
      continue;
    }
    if (ch == ';' && !in_quote) {
      if (!flush_token()) {
        return false;
      }
      continue;
    }
    current.push_back(ch);
  }

  if (escape) {
    error = "EPD operation terminates with an escape character";
    return false;
  }
  if (in_quote) {
    error = "EPD operation contains an unterminated quote";
    return false;
  }
  if (!flush_token()) {
    return false;
  }
  return true;
}

}  // namespace

bool parse_epd_line(std::string_view line, EpdRecord& out_record, std::string& error) {
  error.clear();
  out_record.operations.clear();

  std::string_view cursor = trim_view(line);
  if (cursor.empty()) {
    error = "EPD line is empty";
    return false;
  }

  std::array<std::string_view, 4> fen_tokens{};
  for (std::size_t idx = 0; idx < fen_tokens.size(); ++idx) {
    const std::string_view token = consume_token(cursor);
    if (token.empty()) {
      error = "EPD line missing FEN components";
      return false;
    }
    fen_tokens[idx] = token;
  }

  std::string fen{fen_tokens[0]};
  for (std::size_t idx = 1; idx < fen_tokens.size(); ++idx) {
    fen.append(" ");
    fen.append(fen_tokens[idx]);
  }

  try {
    out_record.position = Position::from_fen(fen, false);
  } catch (const std::exception& ex) {
    error = ex.what();
    return false;
  }

  cursor = trim_view(cursor);
  if (cursor.empty()) {
    return true;
  }

  const char* begin = cursor.data();
  const char* end = cursor.data() + cursor.size();
  const char* ptr = begin;
  const auto skip_spaces = [&]() {
    while (ptr < end && is_space(*ptr)) {
      ++ptr;
    }
  };

  skip_spaces();
  for (int skipped = 0; skipped < 2; ++skipped) {
    const char* number_start = ptr;
    while (ptr < end && std::isdigit(static_cast<unsigned char>(*ptr)) != 0) {
      ++ptr;
    }
    if (ptr == number_start) {
      ptr = number_start;
      break;
    }
    skip_spaces();
  }

  std::string operations = trim_copy(std::string_view(ptr, static_cast<std::size_t>(end - ptr)));
  if (operations.empty()) {
    return true;
  }

  if (!parse_operations(operations, out_record.operations, error)) {
    return false;
  }

  return true;
}

EpdLoadResult load_epd_file(const std::string& path) {
  EpdLoadResult result;
  std::ifstream input(path);
  if (!input) {
    result.errors.push_back(EpdLoadError{
        0, "Failed to open EPD file: " + path, {}});
    return result;
  }

  std::string line;
  std::size_t line_no = 0;
  while (std::getline(input, line)) {
    ++line_no;
    std::string_view view(line);
    view = trim_view(view);
    if (view.empty() || view.front() == '#') {
      continue;
    }
    EpdRecord record;
    std::string parse_error;
    if (parse_epd_line(view, record, parse_error)) {
      result.records.push_back(std::move(record));
    } else {
      result.errors.push_back(EpdLoadError{line_no, parse_error, line});
    }
  }
  return result;
}

}  // namespace bby
