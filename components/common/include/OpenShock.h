#pragma once

#define DISABLE_DEFAULT(TypeName) TypeName() = delete
#define DISABLE_COPY(TypeName)                   \
  TypeName(const TypeName&)            = delete; \
  TypeName& operator=(const TypeName&) = delete
#define DISABLE_MOVE(TypeName)              \
  TypeName(TypeName&&)            = delete; \
  TypeName& operator=(TypeName&&) = delete

#ifndef OPENSHOCK_API_DOMAIN
#error "OPENSHOCK_API_DOMAIN must be defined"
#endif
#ifndef OPENSHOCK_FW_CDN_DOMAIN
#error "OPENSHOCK_FW_CDN_DOMAIN must be defined"
#endif
#ifndef OPENSHOCK_FW_VERSION
#error "OPENSHOCK_FW_VERSION must be defined"
#endif

#define OPENSHOCK_FW_CDN_URL(path) "https://" OPENSHOCK_FW_CDN_DOMAIN path

#define OPENSHOCK_GPIO_INVALID -1

// Boards without a hardwired RF transmitter leave this undefined; the pin is
// then configured at runtime (see RFConfig / the `rftxpin` command). Default to
// INVALID silently, like OPENSHOCK_ESTOP_PIN below - CompatibilityChecks.cpp
// explicitly treats an INVALID default as a supported configuration.
#ifndef OPENSHOCK_RF_TX_GPIO
#define OPENSHOCK_RF_TX_GPIO OPENSHOCK_GPIO_INVALID
#endif

#ifndef OPENSHOCK_ESTOP_PIN
#define OPENSHOCK_ESTOP_PIN OPENSHOCK_GPIO_INVALID
#endif

// Check if OPENSHOCK_FW_USERAGENT is overridden trough compiler flags, if not, generate a default useragent.
#ifndef OPENSHOCK_FW_USERAGENT
#define OPENSHOCK_FW_USERAGENT OPENSHOCK_FW_HOSTNAME "/" OPENSHOCK_FW_VERSION " (ESP-IDF; " OPENSHOCK_FW_BOARD "; " OPENSHOCK_FW_CHIP "; Espressif)"
#endif

namespace OpenShock::Constants {
  const char* const FW_USERAGENT = OPENSHOCK_FW_USERAGENT;
}  // namespace OpenShock::Constants
