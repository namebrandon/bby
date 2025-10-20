#include "debug.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <mutex>
#include <sstream>

namespace bby {
namespace {

std::array<bool, static_cast<std::size_t>(TraceTopic::Count)> &trace_flags() {
  static std::array<bool, static_cast<std::size_t>(TraceTopic::Count)> flags{};
  return flags;
}

std::mutex& trace_mutex() {
  static std::mutex mutex;
  return mutex;
}

TraceWriter& trace_writer() {
  static TraceWriter writer = nullptr;
  return writer;
}

std::string lowercase(std::string_view sv) {
  std::string out(sv.begin(), sv.end());
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

}  // namespace

void set_trace_topic(TraceTopic topic, bool enabled) {
  trace_flags()[static_cast<std::size_t>(topic)] = enabled;
}

bool trace_enabled(TraceTopic topic) {
  return trace_flags()[static_cast<std::size_t>(topic)];
}

void set_trace_writer(TraceWriter writer) {
  std::lock_guard<std::mutex> lock(trace_mutex());
  trace_writer() = writer;
}

std::optional<TraceTopic> trace_topic_from_string(std::string_view token) {
  const std::string norm = lowercase(token);
  if (norm == "search") {
    return TraceTopic::Search;
  }
  if (norm == "qsearch") {
    return TraceTopic::QSearch;
  }
  if (norm == "tt") {
    return TraceTopic::TT;
  }
  if (norm == "eval") {
    return TraceTopic::Eval;
  }
  if (norm == "moves") {
    return TraceTopic::Moves;
  }
  return std::nullopt;
}

std::string_view trace_topic_name(TraceTopic topic) {
  switch (topic) {
    case TraceTopic::Search:
      return "search";
    case TraceTopic::QSearch:
      return "qsearch";
    case TraceTopic::TT:
      return "tt";
    case TraceTopic::Eval:
      return "eval";
    case TraceTopic::Moves:
      return "moves";
    case TraceTopic::Count:
      break;
  }
  return "unknown";
}

void trace_emit(TraceTopic topic, std::string_view message) {
  if (!trace_enabled(topic)) {
    return;
  }
  std::ostringstream oss;
  oss << "trace " << trace_topic_name(topic) << ' ' << message;
  const std::string payload = oss.str();
  std::lock_guard<std::mutex> lock(trace_mutex());
  if (const TraceWriter writer = trace_writer()) {
    writer(topic, payload);
  } else {
    std::cout << "info string " << payload << '\n';
    std::cout.flush();
  }
}

InvariantStatus validate_position(const Position& pos) {
  InvariantStatus status;
  if (!pos.is_sane(&status.message)) {
    status.ok = false;
    if (status.message.empty()) {
      status.message = "unknown invariant violation";
    }
  } else {
    status.ok = true;
    status.message = "position ok";
  }
  return status;
}

}  // namespace bby
