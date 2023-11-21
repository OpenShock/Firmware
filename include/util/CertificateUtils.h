#pragma once

#include <WiFiClientSecure.h>

#include <vector>
#include <cstdint>

namespace OpenShock::CertificateUtils {
  WiFiClientSecure GetSecureClient();
  bool GetHostCertificate(const char* host, std::vector<char>& pem);
} // namespace OpenShock::CertificateUtils
