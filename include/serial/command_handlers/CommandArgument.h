#pragma once

#include <string_view>
#include <vector>

namespace OpenShock::Serial {
  class CommandArgument {
  public:
    std::string_view name;
    std::string_view constraint;
    std::string_view exampleValue;
    std::vector<std::string_view> constraintExtensions;
  };
}  // namespace OpenShock::Serial
