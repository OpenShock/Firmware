#pragma once

// Field parsing helpers shared by every module's config section (its FromJSON): GPIO numbers, enum strings and
// arrays, each falling back to the default the way the rest of a section does.

#include "util/FunctionRef.h"
#include "json/Json.h"

#include <hal/gpio_types.h>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace OpenShock::Config::Parse {
  // Accepts GPIO_NUM_NC (-1, "no pin") or any GPIO number valid on this chip; used for every stored/imported pin.
  [[nodiscard]] std::optional<gpio_num_t> GpioNum(int32_t intVal);

  [[nodiscard]] std::optional<gpio_num_t> JsonGpioNum(JSON::JsonView json, std::string_view name);

  template<typename T>
  bool JsonStrParsed(T& val, JSON::JsonView json, std::string_view name, std::type_identity_t<Util::FunctionRef<std::optional<T>(std::string_view)>> StringParser, T defaultVal)
  {
    JSON::JsonView jsonVal = json[name];
    if (!jsonVal.valid()) {
      val = defaultVal;
      return true;
    }

    // Wrong type or unknown value: fall back to the default like every other field, rather than keeping
    // whatever value `val` held before (the current running config when importing JSON).
    auto str = jsonVal.tryGetStr();
    if (!str) {
      val = defaultVal;
      return false;
    }

    auto parsed = StringParser(*str);
    if (!parsed) {
      val = defaultVal;
      return false;
    }

    val = *parsed;
    return true;
  }

  /// @brief Fills `vec` with every element of `jsonArray` whose T::FromJSON accepts it; the others are dropped.
  template<typename T>
  bool JsonArray(std::vector<T>& vec, JSON::JsonView jsonArray)
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
}  // namespace OpenShock::Config::Parse
