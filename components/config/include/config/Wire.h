#pragma once

// The whole config in the FlatBuffers HubConfig form. It is never stored: it is the shape the local (captive
// portal) protocol's Ready message carries, and the binary form of the rawconfig serial command. Assembled from
// every section's own ToFlatbuffers; parsed back through the JSON importer (HubConfig -> jsonconfig document).

#include "serialization/_fbs/HubConfig_generated.h"
#include "TinyVec.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace OpenShock::Config {
  /// @brief Builds a HubConfig table from every section that has a HubConfig field.
  [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::HubConfig> GetAsFlatBuffer(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData);

  /// @brief The finished HubConfig buffer, secrets included (rawconfig get).
  bool GetRaw(TinyVec<uint8_t>& buffer);

  /// @brief Applies a HubConfig buffer as SaveFromJSON applies a document (rawconfig set). False when it does not
  ///        verify or a section refuses its part.
  bool SetRaw(const uint8_t* buffer, std::size_t size);

  /// @brief The jsonconfig document a HubConfig buffer stands for: what SetRaw applies and how the 1.x config file
  ///        (the same HubConfig, written by the Arduino firmware) is imported. Nullopt when the buffer does not verify.
  [[nodiscard]] std::optional<std::string> HubConfigToJSON(std::span<const uint8_t> buffer);

  /// @brief The largest HubConfig accepted: what the 1.x config partition could hold, which is also far more than
  ///        any real config renders to.
  inline constexpr std::size_t kMaxHubConfigSize = 4096;
}  // namespace OpenShock::Config
