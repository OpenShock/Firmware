#include "MDNSAnnouncer.h"

#include "config/Config.h"

#include <mdns.h>

#include <string>

static const char* TAG = "MDNSAnnouncer";

using namespace OpenShock;

bool MDNSAnnouncer::Init() {
  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to initialize mDNS");
    WiFi.softAPdisconnect(true);
    return false;
  }

  mdns_instance_name_set("OpenShock");

  std::string hostname;
  if (Config::GetWiFiHostname(hostname)) {
    OS_LOGE(TAG, "Failed to get WiFi hostname, reverting to default");
    hostname = OPENSHOCK_FW_HOSTNAME;
  }

  err = mdns_hostname_set(hostname.c_str());
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to set mDNS hostname");
    WiFi.softAPdisconnect(true);
    return false;
  }

  return true;
}

bool MDNSAnnouncer::IsEnabled() {
  mdns_service_add("http", "_tcp", 80);
  return Config::GetCaptivePortalConfig().mdnsEnabled;
}

void MDNSAnnouncer::SetEnabled(bool enabled) {
  Config::SetCaptivePortalConfig({
    .mdnsEnabled = enabled,
  });
}
