#pragma once

// Shared helpers for the config host tests: a sample config with every field moved
// off its default, field-by-field comparison, and driving the real Config:: store
// (Config.cpp) through the in-memory ConfigFs stub.
#include "config/Config.h"
#include "config/RootConfig.h"

#include "fs/ConfigFs.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ConfigTest {
  // Path Config.cpp reads/writes inside the config partition. The Arduino firmware
  // used the same file ("/config" on the "config" littlefs partition).
  constexpr const char* kConfigPath = "/config";

  // Every field differs from RootConfig's defaults.
  OpenShock::Config::RootConfig MakeSampleConfig();

  // Serializes like Config.cpp's trySaveConfig (ToFlatbuffers + FinishHubConfigBuffer).
  std::vector<uint8_t> Serialize(const OpenShock::Config::RootConfig& config, bool withSensitiveData = true);

  // Verifies (same options as Config.cpp) and deserializes a stored buffer.
  bool Deserialize(const uint8_t* data, std::size_t size, OpenShock::Config::RootConfig& out);

  // Seeds the stored config file (nullptr = no file), runs Config::Init, and returns
  // true when Init loaded that file as-is (it rewrites the file only when it fell
  // back to defaults).
  bool InitFrom(const uint8_t* data, std::size_t size);
  inline bool InitFrom(const std::vector<uint8_t>& data)
  {
    return InitFrom(data.data(), data.size());
  }

  // Resets the store to a freshly written default config.
  void InitDefault();

  // The bytes currently stored in the config file (empty if there is none).
  std::vector<uint8_t> StoredFile();

  // Reads back every section through the public Config:: getters.
  OpenShock::Config::RootConfig Snapshot();

  // TEST_ASSERTs that every field of every section matches. When withSensitiveData
  // is false, passwords and the backend auth token are expected to be empty.
  void AssertConfigEqual(const OpenShock::Config::RootConfig& expected, const OpenShock::Config::RootConfig& actual, bool withSensitiveData = true);
  void AssertCredentialsEqual(const OpenShock::Config::WiFiCredentials& expected, const OpenShock::Config::WiFiCredentials& actual, bool withSensitiveData = true);
}  // namespace ConfigTest
