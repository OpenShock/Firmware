#pragma once

#include <IPAddress.h>

#include "StringView.h"

namespace OpenShock {
  bool IPAddressFromStringView(IPAddress& ip, StringView sv);
}
