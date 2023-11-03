#include "CertificateUtils.h"

#include "Logging.h"

#include <mbedtls/pem.h>

const char* const TAG = "CertificateUtils";

WiFiClientSecure OpenShock::CertificateUtils::GetSecureClient() {
  WiFiClientSecure client;

  return client;
}
