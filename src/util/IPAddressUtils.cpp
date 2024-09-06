#include "util/IPAddressUtils.h"

#include "Convert.h"
#include "util/StringUtils.h"

const char* const TAG = "Util::IPAddressUtils";

bool OpenShock::IPV4AddressFromStringView(IPAddress& ip, std::string_view sv) {
  if (sv.empty()) {
    return false;
  }

  std::string_view parts[4];
  if (!OpenShock::TryStringSplit(sv, '.', parts)) {
    return false;  // Must have 4 octets
  }

  std::uint8_t octets[4];
  if (!Convert::ToUInt8(parts[0], octets[0]) || !Convert::ToUInt8(parts[1], octets[1]) || !Convert::ToUInt8(parts[2], octets[2]) || !Convert::ToUInt8(parts[3], octets[3])) {
    return false;
  }

  ip = IPAddress(octets);

  return true;
}
