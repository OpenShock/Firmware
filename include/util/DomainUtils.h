#pragma once

#include <string_view>

namespace OpenShock::DomainUtils {
  std::string_view GetDomainFromUrl(std::string_view url);
}  // namespace OpenShock::DomainUtils
