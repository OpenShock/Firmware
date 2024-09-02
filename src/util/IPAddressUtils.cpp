#include "util/IPAddressUtils.h"

#include "FormatHelpers.h"

const char* const TAG = "Util::IPAddressUtils";

bool OpenShock::IPAddressFromStringView(IPAddress& ip, StringView sv) {
  if (sv.isNullOrEmpty()) {
    return false;
  }

  int octetIndex = 0;
  std::uint8_t octets[4];
  std::uint16_t octetValue = 0;

  std::size_t digits = 0;

  for (std::size_t i = 0; i < sv.length(); ++i) {
    char c = sv[i];

    if (c >= '0' && c <= '9') {
      if (digits > 0 && octetValue == 0) {
        return false;  // No leading zeros allowed
      }
      if (++digits > 3) {
        return false;  // Maximum of 3 digits per octet
      }

      octetValue = (octetValue * 10) + (c - '0');
      if (octetValue > 255) {
        return false;  // Each octet must be between 0 and 255
      }
      continue;
    }

    if (c != '.') {
      return false;  // Only digits and dots allowed
    }
    if (digits == 0) {
      return false;  // No empty octets allowed
    }

    octets[octetIndex++] = static_cast<std::uint8_t>(octetValue);
    if (octetIndex > 3) {
      return false;  // Only 3 dots allowed
    }
    octetValue = 0;
    digits     = 0;
  }

  if (octetIndex != 3 || digits == 0) {
    return false;  // Must have 3 dots and no trailing dot
  }

  octets[octetIndex] = static_cast<std::uint8_t>(octetValue);

  ip = IPAddress(octets);

  return true;
}
