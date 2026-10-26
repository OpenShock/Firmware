#pragma once

// The `backend` config section: the API domain and the hub's auth token. Owned by core (GatewayConnectionManager)
// and kept in NVS through the config framework (config/Section.h); namespace "backend", keys domain and token (str).

#include "config/Section.h"
#include "serialization/_fbs/HubConfig_generated.h"

#include <optional>
#include <string>
#include <string_view>

namespace OpenShock::JSON {
  class JsonView;
}

namespace OpenShock::Config {
  struct BackendConfig {
    BackendConfig();
    BackendConfig(std::string_view domain, std::string_view authToken);

    std::string domain;
    std::string authToken;

    void ToDefault();

    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::BackendConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const;

    /// @brief Reads this section from `partition`; a missing key is its default, a read error fails.
    bool LoadNvs(const char* partition);
    /// @brief Writes the keys that differ from `previous` (every key when null) to `partition`.
    bool SaveNvs(const char* partition, const BackendConfig* previous) const;

    bool FromJSON(JSON::JsonView json);
    void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const;
  };

  Section<BackendConfig>& BackendSection();

  [[nodiscard]] std::optional<BackendConfig> GetBackendConfig();
  bool SetBackendConfig(const BackendConfig& config);
  [[nodiscard]] std::optional<std::string> GetBackendDomain();
  bool SetBackendDomain(std::string domain);
  bool HasBackendAuthToken();
  [[nodiscard]] std::optional<std::string> GetBackendAuthToken();
  bool SetBackendAuthToken(std::string token);
  bool ClearBackendAuthToken();
}  // namespace OpenShock::Config
