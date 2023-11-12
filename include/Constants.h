#pragma once

#include <cstdint>

#ifndef OPENSHOCK_API_DOMAIN
#error "OPENSHOCK_API_DOMAIN must be defined"
#endif

#ifndef OPENSHOCK_API_BASE_URL
#define OPENSHOCK_API_BASE_URL "https://" OPENSHOCK_API_DOMAIN
#endif

#define OPENSHOCK_API_URL(path) OPENSHOCK_API_BASE_URL path

#ifndef OPENSHOCK_FW_VERSION
#error "OPENSHOCK_FW_VERSION must be defined"
#endif

namespace OpenShock::Constants {
  constexpr std::uint8_t GPIO_INVALID = UINT8_MAX;
}  // namespace OpenShock::Constants
