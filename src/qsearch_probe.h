#pragma once
/**
 * @file qsearch_probe.h
 * @brief Lightweight instrumentation hooks for quiescence delta-prune analysis.
 */

#ifndef BBY_ENABLE_QSEARCH_PROBE
#define BBY_ENABLE_QSEARCH_PROBE 0
#endif

#include <cstdint>

#include "board.h"
#include "common.h"

namespace bby {

#if BBY_ENABLE_QSEARCH_PROBE

/**
 * @brief Emit diagnostic details for quiescence delta-prune checks.
 */
bool qsearch_delta_prune_probe(const Position& pos,
                               Move move,
                               Score stand_pat,
                               Score alpha,
                               int margin,
                               int delta_margin,
                               int ply,
                               bool pruned);

#else  // BBY_ENABLE_QSEARCH_PROBE

inline bool qsearch_delta_prune_probe(const Position&,
                                      Move,
                                      Score,
                                      Score,
                                      int,
                                      int,
                                      int,
                                      bool) {
  return false;
}

#endif  // BBY_ENABLE_QSEARCH_PROBE

}  // namespace bby

