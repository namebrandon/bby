#include "uci.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iomanip>
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

struct UciIo {
  std::mutex mutex;
  UciWriter writer{nullptr};
};

void write_line(UciIo& io, const std::string& text) {
  std::lock_guard<std::mutex> lock(io.mutex);
  if (const UciWriter writer = io.writer) {
    writer(text);
  } else {
    std::cout << text << '\n';
    std::cout.flush();
  }
}

UciWriter& thread_local_writer() {
  thread_local UciWriter writer = nullptr;
  return writer;
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

struct SearchSnapshot {
  Position position{};
  SearchResult result{};
  Limits limits{};
  bool stopped{false};
};

class SearchWorker {
 public:
  SearchWorker() : thread_(&SearchWorker::run, this) {}
  ~SearchWorker() { shutdown(); }

  void bind_io(UciIo* io) {
    io_ = io;
  }

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

  bool last_snapshot(SearchSnapshot& out) const {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    if (!has_snapshot_) {
      return false;
    }
    out = last_snapshot_;
    return true;
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

          const bool stopped = stop_flag_.load(std::memory_order_acquire);
          {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            last_snapshot_.position = local;
            last_snapshot_.result = result;
            last_snapshot_.limits = limits;
            last_snapshot_.stopped = stopped;
            has_snapshot_ = true;
          }

          if (io_ == nullptr) {
            BBY_ASSERT(false && "UCI I/O must be bound before search");
            return;
          }

          if (stopped) {
            write_line(*io_, "bestmove 0000");
          } else {
            write_line(*io_, "bestmove " + format_move(result.best));
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
  mutable std::mutex snapshot_mutex_;
  SearchSnapshot last_snapshot_{};
  bool has_snapshot_{false};
  UciIo* io_{nullptr};
};

struct UciState {
  mutable UciIo io{};
  SearchWorker worker{};
  Position pos{Position::from_fen(kStartPositionFen, false)};
  int threads{1};
  int hash_mb{128};
  bool debug{false};
  InitState init;

  explicit UciState(const InitState& init_state)
      : init(init_state) {
    worker.bind_io(&io);
    io.writer = thread_local_writer();
  }
};

void emit_id_block(UciIo& io) {
  write_line(io, std::string("id name ") + std::string(kEngineName));
  write_line(io, std::string("id author ") + std::string(kEngineAuthor));
}

void emit_options(const UciState& state) {
  write_line(state.io, "option name Threads type spin default 1 min 1 max 512 value " +
                             std::to_string(state.threads));
  write_line(state.io, "option name Hash type spin default 128 min 1 max 8192 value " +
                             std::to_string(state.hash_mb));
}

void send_readyok(UciIo& io) {
  write_line(io, "readyok");
}

void send_info(UciIo& io, const std::string& msg) {
  write_line(io, "info string " + msg);
}

void handle_register(UciState& state, std::string_view) {
  send_info(state.io, "registration not required");
}

void handle_ponderhit(UciState& state) {
  send_info(state.io, "ponderhit acknowledged (pondering not implemented)");
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
      send_info(state.io, "invalid FEN supplied to position command");
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
      send_info(state.io, std::string("FEN error: ") + ex.what());
      return;
    }
  } else {
    send_info(state.io, "unknown token after position: " + token);
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
        send_info(state.io, "illegal move '" + move_token + "'");
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
    send_info(state.io, "setoption missing 'name'");
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
    send_info(state.io, "debug log unsupported");
  } else {
    send_info(state.io, "ignored option '" + name + "'");
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

  if (state.worker.is_busy()) {
    state.worker.request_stop();
    state.worker.wait_idle();
  }
  state.worker.start_search(state.pos, limits);
}

void handle_debug(UciState& state, std::string_view args) {
  std::string token = consume_token(args);
  if (token == "on") {
    state.debug = true;
  } else if (token == "off") {
    state.debug = false;
  }
  send_info(state.io, std::string("debug ") + (state.debug ? "on" : "off"));
}

void handle_ucinewgame(UciState& state) {
  state.pos = Position::from_fen(kStartPositionFen, false);
}

void handle_trace(UciState& state, std::string_view args) {
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
    send_info(state.io, message);
    return;
  }

  bool enable = false;
  if (command == "on") {
    enable = true;
  } else if (command == "off") {
    enable = false;
  } else {
    send_info(state.io, "trace usage: trace [status|on|off] <topic>");
    return;
  }

  const std::string topic_token = consume_token(args);
  if (topic_token.empty()) {
    send_info(state.io, "trace requires a topic (search|qsearch|tt|eval|moves)");
    return;
  }
  const auto topic = trace_topic_from_string(topic_token);
  if (!topic) {
    send_info(state.io, "unknown trace topic '" + topic_token + "'");
    return;
  }

  set_trace_topic(*topic, enable);
  std::string response = "trace ";
  response.append(trace_topic_name(*topic));
  response.push_back('=');
  response.append(enable ? "on" : "off");
  send_info(state.io, response);
}

void handle_assert(const UciState& state) {
  const InvariantStatus status = validate_position(state.pos);
  if (status.ok) {
    send_info(state.io, "assert: position ok");
  } else {
    send_info(state.io, "assert failed: " + status.message);
  }
}

void handle_repropack(const UciState& state) {
  std::ostringstream oss;
  SearchSnapshot snapshot;
  const bool have_snapshot = state.worker.last_snapshot(snapshot);
  const Position& repro_pos = have_snapshot ? snapshot.position : state.pos;
  const char* stm = repro_pos.side_to_move() == Color::White ? "white" : "black";
  oss << "repro fen=" << repro_pos.to_fen()
      << " zobrist=" << repro_pos.zobrist()
      << " stm=" << stm
      << " hash_mb=" << state.hash_mb
      << " threads=" << state.threads
      << " halfmove=" << static_cast<int>(repro_pos.halfmove_clock())
      << " fullmove=" << static_cast<int>(repro_pos.fullmove_number());

  if (have_snapshot) {
    oss << " depth=" << snapshot.result.depth
        << " nodes=" << snapshot.result.nodes
        << " stopped=" << (snapshot.stopped ? "true" : "false");
    if (!snapshot.result.pv.line.empty()) {
      oss << " pv=";
      for (std::size_t idx = 0; idx < snapshot.result.pv.line.size(); ++idx) {
        if (idx > 0) {
          oss << ',';
        }
        oss << move_to_uci(snapshot.result.pv.line[idx]);
      }
    }
    const Limits& limits = snapshot.limits;
    if (limits.depth >= 0) {
      oss << " limit_depth=" << limits.depth;
    }
    if (limits.nodes >= 0) {
      oss << " limit_nodes=" << limits.nodes;
    }
    if (limits.movetime_ms >= 0) {
      oss << " limit_movetime_ms=" << limits.movetime_ms;
    }
    if (limits.wtime_ms >= 0) {
      oss << " limit_wtime_ms=" << limits.wtime_ms;
    }
    if (limits.btime_ms >= 0) {
      oss << " limit_btime_ms=" << limits.btime_ms;
    }
  }

  oss << " rng_seed=0x" << std::hex << state.init.options.rng_seed << std::dec;

  oss << " options=Threads:" << state.threads << ",Hash:" << state.hash_mb;
  if (state.debug) {
    oss << ",Debug:on";
  }
  send_info(state.io, oss.str());
}

void handle_bench(UciState& state, std::string_view args) {
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
    send_info(state.io, oss.str());
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
  send_info(state.io, summary.str());
}

void handle_uci(UciState& state) {
  emit_id_block(state.io);
  emit_options(state);
  write_line(state.io, "uciok");
}

bool dispatch_command(UciState& state, std::string_view line, bool allow_shutdown) {
  std::string_view view = line;
  const std::string command = consume_token(view);

  if (command.empty()) {
    return true;
  }

  if (command == "uci") {
    handle_uci(state);
  } else if (command == "isready") {
    state.worker.wait_idle();
    send_readyok(state.io);
  } else if (command == "ucinewgame") {
    if (state.worker.is_busy()) {
      state.worker.request_stop();
      state.worker.wait_idle();
    }
    handle_ucinewgame(state);
  } else if (command == "position") {
    handle_position(state, view);
  } else if (command == "go") {
    handle_go(state, view);
  } else if (command == "stop") {
    state.worker.request_stop();
    state.worker.wait_idle();
    send_info(state.io, "stop acknowledged");
  } else if (command == "ponderhit") {
    handle_ponderhit(state);
  } else if (command == "register") {
    handle_register(state, view);
  } else if (command == "bench") {
    handle_bench(state, view);
  } else if (command == "trace") {
    handle_trace(state, view);
  } else if (command == "assert") {
    handle_assert(state);
  } else if (command == "repropack") {
    handle_repropack(state);
  } else if (command == "quit") {
    if (allow_shutdown) {
      state.worker.shutdown();
    } else {
      state.worker.request_stop();
      state.worker.wait_idle();
    }
    return false;
  } else if (command == "setoption") {
    handle_setoption(state, view);
  } else if (command == "debug") {
    handle_debug(state, view);
  } else {
    send_info(state.io, "unknown command '" + command + "'");
  }

  return true;
}

}  // namespace

void set_uci_writer(UciWriter writer) {
  thread_local_writer() = writer;
}

int uci_main() {
  const InitState init_state = initialize();

  UciState state(init_state);
  std::string line;

  while (std::getline(std::cin, line)) {
    if (!dispatch_command(state, line, true)) {
      break;
    }
  }

  return 0;
}

void uci_fuzz_feed(std::string_view payload) {
  const InitState init_state = initialize();

  UciState state(init_state);
  std::string_view remaining = payload;
  while (!remaining.empty()) {
    const auto newline = remaining.find('\n');
    const std::string_view line =
        (newline == std::string_view::npos) ? remaining : remaining.substr(0, newline);
    if (!dispatch_command(state, line, false)) {
      break;
    }
    if (newline == std::string_view::npos) {
      break;
    }
    remaining.remove_prefix(newline + 1);
  }

  state.worker.wait_idle();
}

}  // namespace bby
