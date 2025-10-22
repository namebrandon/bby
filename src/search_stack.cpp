#include "search_stack.h"

namespace bby {

namespace {
constexpr Score kImprovingSlack = 30;

SearchStack::Frame make_frame() {
  SearchStack::Frame frame{};
  frame.parent_move = Move{};
  frame.captured = PieceType::None;
  frame.static_eval = 0;
  frame.previous_static_eval = 0;
  frame.has_static_eval = false;
  frame.has_previous_eval = false;
  frame.improving = false;
  return frame;
}
}  // namespace

SearchStack::SearchStack() {
  reset();
}

void SearchStack::reset() {
  for (auto& frame : frames_) {
    frame = make_frame();
  }
}

auto SearchStack::frame(int ply) -> Frame& {
  BBY_ASSERT(ply >= 0 && ply < kMaxPly);
  return frames_[static_cast<std::size_t>(ply)];
}

auto SearchStack::frame(int ply) const -> const Frame& {
  BBY_ASSERT(ply >= 0 && ply < kMaxPly);
  return frames_[static_cast<std::size_t>(ply)];
}

void SearchStack::prepare_root() {
  reset();
  frames_[0] = make_frame();
}

void SearchStack::prepare_child(int parent_ply, int child_ply, Move move, PieceType captured) {
  BBY_ASSERT(parent_ply >= -1 && parent_ply < kMaxPly);
  BBY_ASSERT(child_ply >= 0 && child_ply < kMaxPly);
  Frame& frame = frames_[static_cast<std::size_t>(child_ply)];
  frame = make_frame();
  frame.parent_move = move;
  frame.captured = captured;
  if (child_ply >= 2 && frames_[static_cast<std::size_t>(child_ply - 2)].has_static_eval) {
    frame.previous_static_eval = frames_[static_cast<std::size_t>(child_ply - 2)].static_eval;
    frame.has_previous_eval = true;
  }
}

void SearchStack::set_static_eval(int ply, Score eval) {
  BBY_ASSERT(ply >= 0 && ply < kMaxPly);
  Frame& frame = frames_[static_cast<std::size_t>(ply)];
  frame.static_eval = eval;
  frame.has_static_eval = true;
  if (ply >= 2 && frames_[static_cast<std::size_t>(ply - 2)].has_static_eval) {
    frame.previous_static_eval = frames_[static_cast<std::size_t>(ply - 2)].static_eval;
    frame.has_previous_eval = true;
  }
  if (frame.has_previous_eval) {
    frame.improving = eval >= frame.previous_static_eval - kImprovingSlack;
  } else {
    frame.improving = (ply > 0) ? frames_[static_cast<std::size_t>(ply - 1)].improving : false;
  }
}

bool SearchStack::is_improving(int ply) const {
  if (ply < 0 || ply >= kMaxPly) {
    return false;
  }
  return frames_[static_cast<std::size_t>(ply)].improving;
}

}  // namespace bby
