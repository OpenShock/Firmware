#pragma once

#include "serialization/_fbs/ConfigFile_generated.h"

#include <cJSON.h>

namespace OpenShock::Config {
  template <typename T>
  struct ConfigBase {
    virtual void ToDefault() = 0;

    virtual bool FromFlatbuffers(const T* config) = 0;
    virtual flatbuffers::Offset<T> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder) const = 0;

    virtual bool FromJSON(const cJSON* json) = 0;
    virtual cJSON* ToJSON() const = 0;
  };

}  // namespace OpenShock::Config
