#pragma once

#include "config/ConfigBase.h"
#include "enums/DiagnosticsLogLevel.h"

namespace OpenShock::Config {
  /// @brief The owner's opt-ins to crash dump and log upload. Kept whether or not the firmware has uploading compiled in.
  struct DiagnosticsConfig : public ConfigBase<Serialization::Configuration::DiagnosticsConfig> {
    DiagnosticsConfig();
    DiagnosticsConfig(bool coredumpUpload, DiagnosticsLogLevel logUploadLevel);

    bool coredumpUpload;
    DiagnosticsLogLevel logUploadLevel;

    void ToDefault() override;

    bool FromFlatbuffers(const Serialization::Configuration::DiagnosticsConfig* config) override;
    [[nodiscard]] flatbuffers::Offset<Serialization::Configuration::DiagnosticsConfig> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override;

    bool FromJSON(JSON::JsonView json) override;
    void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const override;
  };
}  // namespace OpenShock::Config
