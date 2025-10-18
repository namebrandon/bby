#pragma once
// debug.h -- Trace toggles and validation helpers for diagnostics commands.

#include <optional>
#include <string>
#include <string_view>

#include "board.h"

namespace bby {

enum class TraceTopic : std::uint8_t {
  Search = 0,
  QSearch,
  TT,
  Eval,
  Moves,
  Count
};

void set_trace_topic(TraceTopic topic, bool enabled);
bool trace_enabled(TraceTopic topic);
std::optional<TraceTopic> trace_topic_from_string(std::string_view token);
std::string_view trace_topic_name(TraceTopic topic);

struct InvariantStatus {
  bool ok{true};
  std::string message{"ok"};
};

InvariantStatus validate_position(const Position& pos);

}  // namespace bby
