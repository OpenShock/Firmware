#include "util/IPAddressUtils.h"

#include "Logging.h"

const char* const TAG = "Util::IPAddressUtils";

bool OpenShock::IPAddressFromStringView(IPAddress& ip, StringView sv) {
  if (sv.isNullOrEmpty()) {
    return false;
  }

  std::uint8_t octets[4];
  std::uint32_t octetIdx = 0;
  for (char c : sv) {
    if (c == '.') {
      if (octetIdx == 4) {
        return false;
      }

      octetIdx++;
      continue;
    }

    if (c < '0' || c > '9') {
      return false;
    }

    octets[octetIdx] = (octets[octetIdx] * 10) + (c - '0');
  }

  if (octetIdx != 3) {
    return false;
  }

  ip = IPAddress(octets);

  return true;
}
