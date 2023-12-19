#pragma once

#include "config/ConfigBase.h"

#include <cJSON.h>

#include <string>
#include <vector>

namespace OpenShock::Config::Internal::Utils {
  void FromFbsStr(std::string& str, const flatbuffers::String* fbsStr, const char* defaultStr);
  void FromJsonBool(bool& val, const cJSON* json, const char* name, bool defaultVal);
  void FromJsonU8(std::uint8_t& val, const cJSON* json, const char* name, std::uint8_t defaultVal);
  void FromJsonU16(std::uint16_t& val, const cJSON* json, const char* name, std::uint16_t defaultVal);
  void FromJsonStr(std::string& str, const cJSON* json, const char* name, const char* defaultStr);

  template<typename T, typename U>  // T inherits from ConfigBase<U>
  void FromFbsVec(std::vector<T>& vec, const flatbuffers::Vector<flatbuffers::Offset<U>>* fbsVec) {
    vec.clear();
    if (fbsVec != nullptr) {
      for (auto fbsItem : *fbsVec) {
        T item;
        if (item.FromFlatbuffers(fbsItem)) {
          vec.push_back(std::move(item));
        }
      }
    }
  }
  template<typename T>  // T inherits from ConfigBase<U>
  void FromJsonArray(std::vector<T>& vec, const cJSON* jsonArray) {
    vec.clear();
    if (jsonArray != nullptr) {
      const cJSON* jsonItem = nullptr;
      cJSON_ArrayForEach(jsonItem, jsonArray) {
        T item;
        if (item.FromJSON(jsonItem)) {
          vec.push_back(std::move(item));
        }
      }
    }
  }
}  // namespace OpenShock::Config::Internal::Utils
