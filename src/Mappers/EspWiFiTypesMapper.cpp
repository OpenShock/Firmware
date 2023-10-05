#include "Mappers/EspWiFiTypesMapper.h"

using namespace OpenShock;

const char* Mappers::GetWiFiAuthModeName(wifi_auth_mode_t authMode) {
  switch (authMode) {
    case WIFI_AUTH_OPEN:
      return "Open";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA PSK";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2 PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA/WPA2 PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2 Enterprise";
    case WIFI_AUTH_WPA3_PSK:
      return "WPA3 PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "WPA2/WPA3 PSK";
    case WIFI_AUTH_WAPI_PSK:
      return "WAPI PSK";
    default:
      return nullptr;
  }
}
