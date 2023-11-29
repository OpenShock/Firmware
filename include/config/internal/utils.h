#pragma once

#include "config/ConfigBase.h"

#include <cJSON.h>

namespace OpenShock::Config::Internal::Utils {
  inline void FromFbsStr(std::string& str, const flatbuffers::String* fbsStr, const char* defaultStr) {
    if (fbsStr != nullptr) {
      str = fbsStr->c_str();
    } else {
      str = defaultStr;
    }
  }
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
}  // namespace OpenShock::Config::Internal::Utils
