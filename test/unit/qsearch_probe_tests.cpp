#include "qsearch_probe.h"

#include <cstdlib>

#include <catch2/catch_test_macros.hpp>

namespace {

void set_env(const char* key, const char* value) {
#if defined(_WIN32)
  if (value == nullptr) {
    _putenv_s(key, "");
  } else {
    _putenv_s(key, value);
  }
#else
  if (value == nullptr) {
    ::unsetenv(key);
  } else {
    ::setenv(key, value, 1);
  }
#endif
}

}  // namespace

#if BBY_ENABLE_QSEARCH_PROBE
TEST_CASE("qsearch delta probe respects filters", "[qsearch_probe]") {
  using namespace bby;
  const char* kFen = "8/7p/5k2/5p2/p1p2P2/Pr1pPK2/1P1R3P/8 b - - 0 1";

  set_env("BBY_QSEARCH_PROBE", "1");
  set_env("BBY_QSEARCH_PROBE_FEN", kFen);
  set_env("BBY_QSEARCH_PROBE_MOVE", "b3b2");
  set_env("BBY_QSEARCH_PROBE_PLY", "0");

  Position pos = Position::from_fen(kFen, false);
  const Move capture = make_move(Square::B3, Square::B2, MoveFlag::Capture);

  const bool emitted =
      qsearch_delta_prune_probe(pos, capture, /*stand_pat=*/-200, /*alpha=*/100,
                                /*margin=*/100, /*delta_margin=*/150, /*ply=*/0,
                                /*pruned=*/true);
  REQUIRE(emitted);

  set_env("BBY_QSEARCH_PROBE_MOVE", "a1a2");
  const bool skipped =
      qsearch_delta_prune_probe(pos, capture, /*stand_pat=*/-200, /*alpha=*/100,
                                /*margin=*/100, /*delta_margin=*/150, /*ply=*/0,
                                /*pruned=*/true);
  REQUIRE_FALSE(skipped);

  set_env("BBY_QSEARCH_PROBE_MOVE", "b3b2");
  set_env("BBY_QSEARCH_PROBE_MODE", "all");
  const bool unpruned =
      qsearch_delta_prune_probe(pos, capture, /*stand_pat=*/-200, /*alpha=*/100,
                                /*margin=*/100, /*delta_margin=*/150, /*ply=*/0,
                                /*pruned=*/false);
  REQUIRE(unpruned);

  set_env("BBY_QSEARCH_PROBE", nullptr);
  set_env("BBY_QSEARCH_PROBE_FEN", nullptr);
  set_env("BBY_QSEARCH_PROBE_MOVE", nullptr);
  set_env("BBY_QSEARCH_PROBE_PLY", nullptr);
  set_env("BBY_QSEARCH_PROBE_MODE", nullptr);
}
#else
TEST_CASE("qsearch probe disabled", "[qsearch_probe]") {
  using namespace bby;
  Position pos = Position::from_fen("8/7p/5k2/5p2/p1p2P2/Pr1pPK2/1P1R3P/8 b - - 0 1", false);
  const Move capture = make_move(Square::B3, Square::B2, MoveFlag::Capture);
  REQUIRE_FALSE(qsearch_delta_prune_probe(pos, capture, 0, 0, 0, 0, 0, true));
}
#endif
