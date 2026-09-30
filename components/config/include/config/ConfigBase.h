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
    // Emits this config as its own JSON object. When `name` is non-null the object
    // is added as a named member of the enclosing object; when null it is anonymous
    // (document root or array element). See JSON::objBegin/objEnd.
    virtual void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const = 0;
  };

}  // namespace OpenShock::Config
