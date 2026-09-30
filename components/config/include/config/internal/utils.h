#pragma once

#include "config/ConfigBase.h"

#include "json/Json.h"

#include <hal/gpio_types.h>

#include <string>
#include <vector>

namespace OpenShock::Config::Internal::Utils {
  bool FromU8GpioNum(gpio_num_t& val, uint8_t u8Val);

  void FromFbsStr(std::string& str, const flatbuffers::String* fbsStr, const char* defaultStr);

  bool FromJsonGpioNum(gpio_num_t& val, JSON::JsonView json, std::string_view name);

  template<typename T, typename U>  // T inherits from ConfigBase<U>
  void FromFbsVec(std::vector<T>& vec, const flatbuffers::Vector<flatbuffers::Offset<U>>* fbsVec)
  {
    vec.clear();

    if (fbsVec == nullptr) {
      return;
    }

    for (auto fbsItem : *fbsVec) {
      T item;
      if (item.FromFlatbuffers(fbsItem)) {
        vec.push_back(std::move(item));
      }
    }
  }
  template<typename T>  // T inherits from ConfigBase<T>
  bool FromJsonStrParsed(T& val, JSON::JsonView json, std::string_view name, bool (*StringParser)(T&, std::string_view), T defaultVal)
  {
    JSON::JsonView jsonVal = json[name];
    if (!jsonVal.valid()) {
      val = defaultVal;
      return true;
    }

    std::string_view view;
    if (!jsonVal.tryGetStr(view)) {
      return false;
    }

    if (!StringParser(val, view)) {
      return false;
    }

    return true;
  }
  template<typename T>  // T inherits from ConfigBase<T>
  bool FromJsonArray(std::vector<T>& vec, JSON::JsonView jsonArray)
  {
    vec.clear();
    if (!jsonArray.isArray()) {
      return true;
    }

    const int count = jsonArray.count();
    for (int i = 0; i < count; ++i) {
      T item;
      if (item.FromJSON(jsonArray.at(i))) {
        vec.push_back(std::move(item));
      }
    }

    return true;
  }
}  // namespace OpenShock::Config::Internal::Utils
