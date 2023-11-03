#pragma once

#include <WiFiClientSecure.h>

#include <vector>
#include <cstdint>

namespace OpenShock::CertificateUtils {
  WiFiClientSecure GetSecureClient();
} // namespace OpenShock::CertificateUtils
