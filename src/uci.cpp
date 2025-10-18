#include "uci.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "bbinit.h"
#include "bench.h"
#include "board.h"
#include "debug.h"
#include "perft.h"
#include "search.h"
#include "searchparams.h"
namespace bby {
namespace {

std::mutex g_io_mutex;

void write_line(const std::string& text) {
  std::lock_guard<std::mutex> lock(g_io_mutex);
  std::cout << text << '\n';
}

template <typename T, std::size_t Capacity>
class SpscQueue {
 public:
  static_assert(Capacity > 1, "Capacity must be greater than 1");

  bool push(const T& item) {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t next = increment(head);
    if (next == tail_.load(std::memory_order_acquire)) {
      return false;
    }
    buffer_[head] = item;
    head_.store(next, std::memory_order_release);
    return true;
  }

  bool pop(T& out) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      return false;
    }
    out = buffer_[tail];
    tail_.store(increment(tail), std::memory_order_release);
    return true;
  }

 private:
  constexpr std::size_t increment(std::size_t idx) const {
    return (idx + 1) % Capacity;
  }

  std::array<T, Capacity> buffer_{};
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

constexpr std::string_view kEngineName = "Brilliant, But Why?";
constexpr std::string_view kEngineAuthor = "BBY Team";
constexpr std::string_view kStartPositionFen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

struct UciState {
  Position pos = Position::from_fen(kStartPositionFen, false);
  int threads{1};
  int hash_mb{128};
  bool debug{false};
};

std::string consume_token(std::string_view& view) {
  const auto first = view.find_first_not_of(' ');
  if (first == std::string_view::npos) {
    view = std::string_view{};
    return {};
  }
  view.remove_prefix(first);
  const auto end = view.find(' ');
  const auto token = view.substr(0, end);
  if (end == std::string_view::npos) {
    view = std::string_view{};
  } else {
    view.remove_prefix(end + 1);
  }
  return std::string(token);
}

std::optional<std::int64_t> parse_int(std::string_view token) {
  std::int64_t value = 0;
  const auto* begin = token.data();
  const auto* end = begin + token.size();
  if (std::from_chars(begin, end, value).ec == std::errc{}) {
    return value;
  }
  return std::nullopt;
}

Move find_uci_move(const Position& pos, std::string_view token) {
  if (token.size() < 4) {
    return Move{};
  }
  const Square from = square_from_string(token.substr(0, 2));
  const Square to = square_from_string(token.substr(2, 2));
  if (from == Square::None || to == Square::None) {
    return Move{};
  }
  PieceType promo = PieceType::None;
  if (token.size() == 5) {
    switch (token[4]) {
      case 'q':
      case 'Q':
        promo = PieceType::Queen;
        break;
      case 'r':
      case 'R':
        promo = PieceType::Rook;
        break;
      case 'b':
      case 'B':
        promo = PieceType::Bishop;
        break;
      case 'n':
      case 'N':
        promo = PieceType::Knight;
        break;
      default:
        return Move{};
    }
  }

  MoveList moves;
  pos.generate_moves(moves, GenStage::All);
  for (const Move move : moves) {
    if (from_square(move) != from || to_square(move) != to) {
      continue;
    }
    const PieceType move_promo = promotion_type(move);
    if ((move_promo == PieceType::None && promo == PieceType::None) ||
        (move_promo == promo)) {
      return move;
    }
  }
  return Move{};
}

std::string format_move(const Move move) {
  if (move.is_null()) {
    return "0000";
  }
  std::string result;
  result += square_to_string(from_square(move));
  result += square_to_string(to_square(move));
  const PieceType promo = promotion_type(move);
  if (promo != PieceType::None) {
    char suffix = 'q';
    switch (promo) {
      case PieceType::Queen:
        suffix = 'q';
        break;
      case PieceType::Rook:
        suffix = 'r';
        break;
      case PieceType::Bishop:
        suffix = 'b';
        break;
      case PieceType::Knight:
        suffix = 'n';
        break;
      default:
        suffix = 'q';
        break;
    }
    result.push_back(suffix);
  }
  return result;
}

enum class WorkerCommandType { Start, Stop, Quit };

struct SearchCommand {
  WorkerCommandType type{WorkerCommandType::Stop};
  Position position{};
  Limits limits{};
};

class SearchWorker {
 public:
  SearchWorker() : thread_(&SearchWorker::run, this) {}
  ~SearchWorker() { shutdown(); }

  void start_search(const Position& pos, const Limits& limits) {
    busy_.store(true, std::memory_order_release);
    SearchCommand cmd;
    cmd.type = WorkerCommandType::Start;
    cmd.position = pos;
    cmd.limits = limits;
    push(cmd);
  }

  void request_stop() {
    if (!busy_.load(std::memory_order_acquire)) {
      return;
    }
    SearchCommand cmd;
    cmd.type = WorkerCommandType::Stop;
    push(cmd);
  }

  void wait_idle() {
    if (!busy_.load(std::memory_order_acquire)) {
      return;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    ready_cv_.wait(lock, [&] { return !busy_.load(std::memory_order_acquire); });
  }

  bool is_busy() const {
    return busy_.load(std::memory_order_acquire);
  }

  void shutdown() {
    bool expected = false;
    if (shutdown_requested_.compare_exchange_strong(expected, true)) {
      SearchCommand cmd;
      cmd.type = WorkerCommandType::Quit;
      push(cmd);
      if (thread_.joinable()) {
        thread_.join();
      }
    }
  }

 private:
  void push(const SearchCommand& cmd) {
    while (!queue_.push(cmd)) {
      std::this_thread::yield();
    }
  }

  void run() {
    SearchCommand cmd;
    while (true) {
      if (!queue_.pop(cmd)) {
        std::this_thread::yield();
        continue;
      }
      switch (cmd.type) {
        case WorkerCommandType::Start: {
          busy_.store(true, std::memory_order_release);
          stop_flag_.store(false, std::memory_order_release);

          Position local = cmd.position;
          Limits limits = cmd.limits;
          SearchResult result = search(local, limits);

          if (stop_flag_.load(std::memory_order_acquire)) {
            write_line("bestmove 0000");
          } else {
            write_line("bestmove " + format_move(result.best));
          }

          busy_.store(false, std::memory_order_release);
          ready_cv_.notify_all();
          break;
        }
        case WorkerCommandType::Stop:
          stop_flag_.store(true, std::memory_order_release);
          break;
        case WorkerCommandType::Quit:
          return;
      }
    }
  }

  SpscQueue<SearchCommand, 32> queue_{};
  std::thread thread_;
  std::atomic<bool> shutdown_requested_{false};
  std::atomic<bool> stop_flag_{false};
  std::atomic<bool> busy_{false};
  std::mutex mutex_;
  std::condition_variable ready_cv_;
};

SearchWorker g_worker;

void emit_id_block() {
  write_line(std::string("id name ") + std::string(kEngineName));
  write_line(std::string("id author ") + std::string(kEngineAuthor));
}

void emit_options(const UciState& state) {
  write_line("option name Threads type spin default 1 min 1 max 512 value " +
             std::to_string(state.threads));
  write_line("option name Hash type spin default 128 min 1 max 8192 value " +
             std::to_string(state.hash_mb));
}

void send_readyok() {
  write_line("readyok");
}

void send_info(const std::string& msg) {
  write_line("info string " + msg);
}

void handle_register(std::string_view) {
  send_info("registration not required");
}

void handle_ponderhit() {
  send_info("ponderhit acknowledged (pondering not implemented)");
}

void handle_position(UciState& state, std::string_view args) {
  std::string_view view = args;
  std::string token = consume_token(view);
  if (token == "startpos" || token.empty()) {
    state.pos = Position::from_fen(kStartPositionFen, false);
    token = consume_token(view);
  } else if (token == "fen") {
    std::vector<std::string> fen_fields;
    fen_fields.reserve(6);
    while (fen_fields.size() < 6) {
      const std::string field = consume_token(view);
      if (field.empty() || field == "moves") {
        token = field;
        break;
      }
      fen_fields.push_back(field);
      if (view.empty()) {
        token = consume_token(view);
        break;
      }
    }
    if (fen_fields.size() < 4) {
      send_info("invalid FEN supplied to position command");
      return;
    }
    std::string fen_string;
    for (std::size_t i = 0; i < fen_fields.size(); ++i) {
      if (i > 0) fen_string.push_back(' ');
      fen_string += fen_fields[i];
    }
    try {
      state.pos = Position::from_fen(fen_string, false);
    } catch (const std::exception& ex) {
      send_info(std::string("FEN error: ") + ex.what());
      return;
    }
  } else {
    send_info("unknown token after position: " + token);
    return;
  }

  if (token != "moves") {
    token = consume_token(view);
  }

  if (token == "moves") {
    while (true) {
      const std::string move_token = consume_token(view);
      if (move_token.empty()) {
        break;
      }
      const Move mv = find_uci_move(state.pos, move_token);
      if (mv.is_null()) {
        send_info("illegal move '" + move_token + "'");
        break;
      }
      Undo undo;
      state.pos.make(mv, undo);
    }
  }
}

void handle_setoption(UciState& state, std::string_view args) {
  std::string_view view = args;
  std::string token = consume_token(view);  // expect "name"
  if (token != "name") {
    send_info("setoption missing 'name'");
    return;
  }
  std::string name;
  while (true) {
    token = consume_token(view);
    if (token.empty() || token == "value") {
      break;
    }
    if (!name.empty()) {
      name.push_back(' ');
    }
    name += token;
  }
  std::string value;
  if (token == "value") {
    while (true) {
      const std::string part = consume_token(view);
      if (part.empty()) {
        break;
      }
      if (!value.empty()) {
        value.push_back(' ');
      }
      value += part;
    }
  }
  if (name == "Hash") {
    if (auto parsed = parse_int(value)) {
      state.hash_mb = static_cast<int>(std::clamp<std::int64_t>(*parsed, 1, 8192));
    }
  } else if (name == "Threads") {
    if (auto parsed = parse_int(value)) {
      state.threads = static_cast<int>(std::clamp<std::int64_t>(*parsed, 1, 512));
    }
  } else if (name == "Debug Log File") {
    send_info("debug log unsupported");
  } else {
    send_info("ignored option '" + name + "'");
  }
}

void handle_go(UciState& state, std::string_view args) {
  Limits limits;
  std::string_view view = args;
  while (!view.empty()) {
    const std::string token = consume_token(view);
    if (token.empty()) {
      break;
    }
    if (token == "wtime") {
      if (auto val = parse_int(consume_token(view))) {
        limits.wtime_ms = *val;
      }
    } else if (token == "btime") {
      if (auto val = parse_int(consume_token(view))) {
        limits.btime_ms = *val;
      }
    } else if (token == "winc") {
      if (auto val = parse_int(consume_token(view))) {
        limits.winc_ms = *val;
      }
    } else if (token == "binc") {
      if (auto val = parse_int(consume_token(view))) {
        limits.binc_ms = *val;
      }
    } else if (token == "movetime") {
      if (auto val = parse_int(consume_token(view))) {
        limits.movetime_ms = *val;
      }
    } else if (token == "depth") {
      if (auto val = parse_int(consume_token(view))) {
        limits.depth = static_cast<std::int16_t>(*val);
      }
    } else if (token == "nodes") {
      if (auto val = parse_int(consume_token(view))) {
        limits.nodes = *val;
      }
    } else if (token == "infinite") {
      limits.infinite = true;
    } else if (token == "ponder") {
      // TODO: implement ponder support
    }
  }

  if (g_worker.is_busy()) {
    g_worker.request_stop();
    g_worker.wait_idle();
  }
  g_worker.start_search(state.pos, limits);
}

void handle_debug(UciState& state, std::string_view args) {
  std::string token = consume_token(args);
  if (token == "on") {
    state.debug = true;
  } else if (token == "off") {
    state.debug = false;
  }
  send_info(std::string("debug ") + (state.debug ? "on" : "off"));
}

void handle_ucinewgame(UciState& state) {
  state.pos = Position::from_fen(kStartPositionFen, false);
}

void handle_trace(std::string_view args) {
  std::string command = consume_token(args);
  if (command.empty() || command == "status") {
    std::string message = "trace:";
    for (int idx = 0; idx < static_cast<int>(TraceTopic::Count); ++idx) {
      const auto topic = static_cast<TraceTopic>(idx);
      message.push_back(' ');
      message.append(trace_topic_name(topic));
      message.push_back('=');
      message.append(trace_enabled(topic) ? "on" : "off");
    }
    send_info(message);
    return;
  }

  bool enable = false;
  if (command == "on") {
    enable = true;
  } else if (command == "off") {
    enable = false;
  } else {
    send_info("trace usage: trace [status|on|off] <topic>");
    return;
  }

  const std::string topic_token = consume_token(args);
  if (topic_token.empty()) {
    send_info("trace requires a topic (search|qsearch|tt|eval|moves)");
    return;
  }
  const auto topic = trace_topic_from_string(topic_token);
  if (!topic) {
    send_info("unknown trace topic '" + topic_token + "'");
    return;
  }

  set_trace_topic(*topic, enable);
  std::string response = "trace ";
  response.append(trace_topic_name(*topic));
  response.push_back('=');
  response.append(enable ? "on" : "off");
  send_info(response);
}

void handle_assert(const UciState& state) {
  const InvariantStatus status = validate_position(state.pos);
  if (status.ok) {
    send_info("assert: position ok");
  } else {
    send_info("assert failed: " + status.message);
  }
}

void handle_repropack(const UciState& state) {
  std::ostringstream oss;
  const char* stm = state.pos.side_to_move() == Color::White ? "white" : "black";
  oss << "repro fen=" << state.pos.to_fen()
      << " zobrist=" << state.pos.zobrist()
      << " stm=" << stm
      << " hash_mb=" << state.hash_mb
      << " threads=" << state.threads
      << " halfmove=" << static_cast<int>(state.pos.halfmove_clock())
      << " fullmove=" << static_cast<int>(state.pos.fullmove_number());
  send_info(oss.str());
}

void handle_bench(std::string_view args) {
  int depth = 4;
  if (!args.empty()) {
    const std::string token = consume_token(args);
    if (auto parsed = parse_int(token)) {
      depth = static_cast<int>(*parsed);
    }
  }
  if (depth < 1) {
    depth = 1;
  }

  std::uint64_t total_nodes = 0;
  std::uint64_t total_ms = 0;
  for (std::size_t idx = 0; idx < kBenchFens.size(); ++idx) {
    const auto fen = kBenchFens[idx];
    Position pos = Position::from_fen(fen, false);
    const auto start = std::chrono::steady_clock::now();
    const std::uint64_t nodes = perft(pos, depth);
    const auto stop = std::chrono::steady_clock::now();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
    total_nodes += nodes;
    total_ms += static_cast<std::uint64_t>(elapsed_ms);

    std::ostringstream oss;
    oss << "bench index=" << (idx + 1) << '/' << kBenchFens.size()
        << " depth=" << depth
        << " nodes=" << nodes
        << " time_ms=" << elapsed_ms;
    send_info(oss.str());
  }

  std::ostringstream summary;
  summary << "bench summary positions=" << kBenchFens.size()
          << " depth=" << depth
          << " nodes=" << total_nodes
          << " time_ms=" << total_ms;
  if (total_ms > 0) {
    const long double nps_ld = (static_cast<long double>(total_nodes) * 1000.0L) /
                               static_cast<long double>(total_ms);
    summary << " nps=" << static_cast<std::uint64_t>(nps_ld);
  }
  send_info(summary.str());
}

void handle_uci(const UciState& state) {
  emit_id_block();
  emit_options(state);
  write_line("uciok");
}

}  // namespace

int uci_main() {
  initialize();

  UciState state;
  std::string line;

  while (std::getline(std::cin, line)) {
    std::string_view view(line);
    const std::string command = consume_token(view);

    if (command == "uci") {
      handle_uci(state);
    } else if (command == "isready") {
      g_worker.wait_idle();
      send_readyok();
    } else if (command == "ucinewgame") {
      if (g_worker.is_busy()) {
        g_worker.request_stop();
        g_worker.wait_idle();
      }
      handle_ucinewgame(state);
    } else if (command == "position") {
      handle_position(state, view);
    } else if (command == "go") {
      handle_go(state, view);
    } else if (command == "stop") {
      g_worker.request_stop();
      g_worker.wait_idle();
      send_info("stop acknowledged");
    } else if (command == "ponderhit") {
      handle_ponderhit();
    } else if (command == "register") {
      handle_register(view);
    } else if (command == "bench") {
      handle_bench(view);
    } else if (command == "trace") {
      handle_trace(view);
    } else if (command == "assert") {
      handle_assert(state);
    } else if (command == "repropack") {
      handle_repropack(state);
    } else if (command == "quit") {
      g_worker.shutdown();
      break;
    } else if (command == "setoption") {
      handle_setoption(state, view);
    } else if (command == "debug") {
      handle_debug(state, view);
    } else if (!command.empty()) {
      send_info("unknown command '" + command + "'");
    }
  }

  return 0;
}

}  // namespace bby
