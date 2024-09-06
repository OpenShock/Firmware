#include "util/IPAddressUtils.h"

#include "intconv.h"

const char* const TAG = "Util::IPAddressUtils";

bool OpenShock::IPV4AddressFromStringView(IPAddress& ip, StringView sv) {
  if (sv.isNullOrEmpty()) {
    return false;
  }

  auto parts = sv.split('.');
  if (parts.size() != 4) {
    return false;  // Must have 4 octets
  }

  std::uint8_t octets[4];
  if (!IntConv::stou8(parts[0], octets[0]) || !IntConv::stou8(parts[1], octets[1]) || !IntConv::stou8(parts[2], octets[2]) || !IntConv::stou8(parts[3], octets[3])) {
    return false;
  }

  ip = IPAddress(octets);

  return true;
}
