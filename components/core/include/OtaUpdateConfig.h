#pragma once

// The `otaUpdate` config section: update channel, checks and approval, plus the step of an update in flight.
// It lives in core, the lowest component that uses it: the gateway reports the update step, the captive portal edits
// the channel, and the ota component (above both) runs updates. Kept in NVS through the config framework
// (config/Section.h); namespace "ota", keys enabled, domain, channel, chk_boot, chk_period, chk_min, backend_mgmt,
// manual, update_id, step.

#include "config/Section.h"
#include "enums/FirmwareBootType.h"
#include "enums/OtaUpdateChannel.h"
#include "enums/OtaUpdateStep.h"
#include "serialization/_fbs/HubConfig_generated.h"

#include "util/FunctionRef.h"

#include <cstdint>
#include <optional>
#include <string>

namespace OpenShock::JSON {
  class JsonView;
}

namespace OpenShock::Config {
  struct OtaUpdateConfig {
    OtaUpdateConfig();  // The defaults: the one place they are defined
    OtaUpdateConfig(
      bool isEnabled, std::string cdnDomain, OtaUpdateChannel updateChannel, bool checkOnStartup, bool checkPeriodically, uint16_t checkInterval, bool allowBackendManagement, bool requireManualApproval, int32_t updateId, OtaUpdateStep updateStep
    );

    bool isEnabled;
    std::string cdnDomain;
    OtaUpdateChannel updateChannel;
    bool checkOnStartup;
    bool checkPeriodically;
    uint16_t checkInterval;
    bool allowBackendManagement;
    bool requireManualApproval;
    int32_t updateId;
    OtaUpdateStep updateStep;

    /// @brief No rules to apply yet. The section runs it on every value it takes in.
    void Normalize() { }

    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::OtaUpdateConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const;

    /// @brief Reads this section from `partition`; a missing key is its default, a read error fails.
    bool LoadNvs(const char* partition);
    /// @brief Writes the keys that differ from `previous` (every key when null) to `partition`.
    bool SaveNvs(const char* partition, const OtaUpdateConfig* previous) const;

    bool FromJSON(JSON::JsonView json);
    void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const;
  };

  Section<OtaUpdateConfig>& OtaUpdateSection();

  [[nodiscard]] std::optional<OtaUpdateConfig> GetOtaUpdateConfig();
  bool SetOtaUpdateConfig(const OtaUpdateConfig& config);
  /// @brief Read-modify-write that no other writer can interleave with. `update` edits the current config in place and
  ///        returns whether it changed anything (nothing is saved otherwise). It runs under the config write lock: keep
  ///        it to plain field updates.
  bool UpdateOtaUpdateConfig(Util::FunctionRef<bool(OtaUpdateConfig&)> update);

  [[nodiscard]] std::optional<int32_t> GetOtaUpdateId();
  bool SetOtaUpdateId(int32_t updateId);
  [[nodiscard]] std::optional<OtaUpdateStep> GetOtaUpdateStep();
  bool SetOtaUpdateStep(OtaUpdateStep updateStep);
  /// @brief Sets the step only if it is still `expected`, so a stale writer can't clobber an update started meanwhile.
  /// @return True if the step was `expected` and is now `desired`
  bool CompareExchangeOtaUpdateStep(OtaUpdateStep expected, OtaUpdateStep desired);
}  // namespace OpenShock::Config
