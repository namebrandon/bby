#pragma once
/**
 * @file epd.h
 * @brief Extended Position Description (EPD) parsing and loading utilities.
 *
 * EPD parsing guarantees that each accepted record yields a fully constructed
 * `Position` object and collects opcode/value pairs exactly as provided after
 * canonical trimming. Loading a file walks every non-comment line and returns
 * both parsed records and any syntax errors that were encountered so callers
 * can surface aggregated diagnostics. Complexity is linear in the total input
 * length.
 */

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "board.h"

namespace bby {

/** Represents a single EPD entry with its decoded position and operations. */
struct EpdRecord {
  Position position{};
  std::map<std::string, std::string> operations;
};

/** Describes a parsing error captured while loading an EPD file. */
struct EpdLoadError {
  std::size_t line{0};
  std::string message;
  std::string content;
};

/**
 * Aggregates the results of loading an EPD file, exposing both the parsed
 * records and any malformed lines. Callers should treat non-empty `errors` as
 * a signal to surface diagnostics while still operating on the successful
 * subset.
 */
struct EpdLoadResult {
  std::vector<EpdRecord> records;
  std::vector<EpdLoadError> errors;

  [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

/**
 * Parse a single EPD line into an `EpdRecord`.
 * @param line The source text; surrounding whitespace and trailing semicolons
 *        are ignored.
 * @param out_record Output slot for the parsed record when parsing succeeds.
 * @param error Populated with a human-readable description on failure.
 * @return `true` when parsing succeeds, `false` otherwise.
 */
bool parse_epd_line(std::string_view line, EpdRecord& out_record, std::string& error);

/**
 * Load an EPD file from disk, returning every successfully parsed record along
 * with a collection of errors for malformed lines. Comments (lines beginning
 * with '#') and blank lines are skipped. The function never throws; it records
 * I/O issues as a single error entry.
 */
EpdLoadResult load_epd_file(const std::string& path);

}  // namespace bby

