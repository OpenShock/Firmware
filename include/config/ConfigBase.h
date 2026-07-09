#pragma once

#include "serialization/_fbs/HubConfig_generated.h"

#include "json/Json.h"

#include <json_generator.h>

namespace OpenShock::Config {
  template<typename T>
  struct ConfigBase {
    virtual void ToDefault() = 0;

    virtual bool FromFlatbuffers(const T* config)                                                                                     = 0;
    [[nodiscard]] virtual flatbuffers::Offset<T> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const = 0;

    virtual bool FromJSON(JSON::JsonView json) = 0;
    // Emits this config's members into the already-open current JSON object; the
    // caller is responsible for opening/closing it (json_gen_{start,push}_object).
    virtual void ToJSON(json_gen_str_t* gen, bool withSensitiveData) const = 0;
  };

}  // namespace OpenShock::Config
