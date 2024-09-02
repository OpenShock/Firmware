#pragma once

#include <IPAddress.h>

#include "StringView.h"

namespace OpenShock {
  bool IPV4AddressFromStringView(IPAddress& ip, StringView sv);
}
