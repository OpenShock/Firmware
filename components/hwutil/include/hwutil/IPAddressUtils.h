#pragma once

#include <IPAddress.h>

#include <string_view>

namespace OpenShock {
  bool IPV4AddressFromStringView(IPAddress& ip, std::string_view sv);
}
