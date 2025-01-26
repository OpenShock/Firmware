#pragma once

#include <string_view>

namespace OpenShock::Serial {
  typedef void (*CommandHandler)(std::string_view arg, bool isAutomated);
}  // namespace OpenShock::Serial
