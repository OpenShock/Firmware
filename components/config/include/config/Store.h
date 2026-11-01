#pragma once

// The config store: where every module's section (config/Section.h) lives, and the operations over all of them.
//
// The store is the NVS partition labelled `cfg`. A hub whose `cfg` cannot be used runs on defaults in RAM and says so.
//
// The `root` namespace is the store's own: `schema` (u8) marks a store that has been filled. An unmarked store is
// filled once, from every section's defaults.

#include <string>
#include <string_view>

namespace OpenShock::Config {
  /// @brief Opens the store, fills it if it is empty, then loads every section. Call once, after Board::Init (section
  ///        defaults read the board) and before anything reads a section.
  void Init();

  /// @brief The partition the config is stored in ("cfg"), or null when it only lives in RAM.
  [[nodiscard]] const char* StorageLabel();

  /**
   * @brief Resets every section to its defaults: erases its namespaces and writes the defaults.
   *
   * @note A restart after calling this function is HIGHLY recommended.
   */
  void FactoryReset();

  /// @brief The whole config as one JSON object, one member per section (the jsonconfig serial command, backups).
  std::string GetAsJSON(bool withSensitiveData);

  /// @brief Applies a whole-config JSON object. Every section parses first and nothing is saved unless all of them
  ///        accept their part; a section given no member resets to its defaults. The saves themselves are per key
  ///        (NVS has no transactions): if one fails, the sections saved before it keep their new values.
  bool SaveFromJSON(std::string_view json);
}  // namespace OpenShock::Config
