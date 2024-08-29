#pragma once

#include "config/ConfigBase.h"
#include "Logging.h"

#include <cJSON.h>
#include <IPAddress.h>

#include <string>
#include <vector>

namespace OpenShock::Config::Internal::Utils {
  void FromFbsStr(std::string& str, const flatbuffers::String* fbsStr, const char* defaultStr);
  bool FromFbsIPAddress(IPAddress& ip, const flatbuffers::String* fbsIP, const IPAddress& defaultIP);
  bool FromJsonBool(bool& val, const cJSON* json, const char* name, bool defaultVal);
  bool FromJsonU8(uint8_t& val, const cJSON* json, const char* name, uint8_t defaultVal);
  bool FromJsonU16(uint16_t& val, const cJSON* json, const char* name, uint16_t defaultVal);
  bool FromJsonI32(int32_t& val, const cJSON* json, const char* name, int32_t defaultVal);
  bool FromJsonStr(std::string& str, const cJSON* json, const char* name, const char* defaultStr);
  bool FromJsonIPAddress(IPAddress& ip, const cJSON* json, const char* name, const IPAddress& defaultIP);

  template<typename T, typename U> // T inherits from ConfigBase<U>
  void FromFbsVec(std::vector<T>& vec, const flatbuffers::Vector<flatbuffers::Offset<U>>* fbsVec) {
    vec.clear();

    if (fbsVec == nullptr) {
      return;
    }

    for (auto fbsItem : *fbsVec) {
      T item;
      if (item.FromFlatbuffers(fbsItem)) {
        vec.emplace_back(std::move(item));
      }
    }
  }
  template<typename T>  // T inherits from ConfigBase<T>
  bool FromJsonStrParsed(T& val, const cJSON* json, const char* name, bool (*StringParser)(T&, const char*), T defaultVal) {
    const cJSON* jsonVal = cJSON_GetObjectItemCaseSensitive(json, name);
    if (jsonVal == nullptr) {
      val = defaultVal;
      return true;
    }

    if (cJSON_IsString(jsonVal) == 0) {
      return false;
    }

    if (!StringParser(val, jsonVal->valuestring)) {
      return false;
    }

    return true;
  }
  template<typename T>  // T inherits from ConfigBase<T>
  bool FromJsonArray(std::vector<T>& vec, const cJSON* jsonArray) {
    vec.clear();
    if (jsonArray == nullptr) {
      return true;
    }

    const cJSON* jsonItem = nullptr;
    cJSON_ArrayForEach(jsonItem, jsonArray) {
      T item;
      if (item.FromJSON(jsonItem)) {
        vec.emplace_back(std::move(item));
      }
    }

    return true;
  }
}  // namespace OpenShock::Config::Internal::Utils
