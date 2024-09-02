#include "util/CertificateUtils.h"

const char* const TAG = "CertificateUtils";

#include "Logging.h"

#include <mbedtls/pem.h>

const char* const PEM_HEADER = "-----BEGIN CERTIFICATE-----\n";
const char* const PEM_FOOTER = "-----END CERTIFICATE-----\n";

extern const uint8_t* const rootca_crt_bundle_start asm("_binary_certificates_x509_crt_bundle_start");

WiFiClientSecure OpenShock::CertificateUtils::GetSecureClient() {
  WiFiClientSecure client;

  client.setCACertBundle(rootca_crt_bundle_start);

  return client;
}

bool OpenShock::CertificateUtils::GetHostCertificate(const char* host, std::vector<char>& pem) {
  WiFiClientSecure client = GetSecureClient();

  client.connect(host, 443);

  if (client.connected() == 0) {
    ESP_LOGE(TAG, "Failed to connect to host %s", host);
    return false;
  }

  ESP_LOGD(TAG, "Connected to host %s, fetching certificate", host);

  const mbedtls_x509_crt* cert = client.getPeerCertificate();

  if (cert == nullptr) {
    ESP_LOGE(TAG, "Certificate is null");
    return false;
  }

  const mbedtls_x509_buf& der = cert->raw;

  uint8_t c;
  std::size_t pemLen;
  mbedtls_pem_write_buffer(PEM_HEADER, PEM_FOOTER, der.p, der.len, &c, 1, &pemLen);

  pem.resize(pemLen);
  int retval = mbedtls_pem_write_buffer(PEM_HEADER, PEM_FOOTER, der.p, der.len, reinterpret_cast<uint8_t*>(pem.data()), pem.size(), nullptr);
  if (retval != 0) {
    ESP_LOGE(TAG, "Failed to write pem buffer: %d", retval);
    return false;
  }

  ESP_LOGD(TAG, "Successfully fetched certificate from host %s", host);

  return true;
}
