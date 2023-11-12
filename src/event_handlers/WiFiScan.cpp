#include "event_handlers/WiFiScan.h"

#include "CaptivePortal.h"
#include "Config.h"
#include "serialization/WSLocal.h"
#include "wifi/WiFiManager.h"
#include "wifi/WiFiNetwork.h"
#include "wifi/WiFiScanManager.h"
#include "wifi/WiFiScanStatus.h"

const char* const TAG = "WiFiScanEventHandlers";

static std::uint64_t s_scanStatusChangedHandle     = 0;
static std::uint64_t s_scanNetworkDiscoveredHandle = 0;

using namespace OpenShock;

void _scanStatusChangedHandler(OpenShock::WiFiScanStatus status) {
  Serialization::Local::SerializeWiFiScanStatusChangedEvent(status, CaptivePortal::BroadcastMessageBIN);
}

void _scanNetworkDiscoveredHandler(const wifi_ap_record_t* record) {
  std::uint8_t id = Config::GetWiFiCredentialsIDbyBSSIDorSSID(record->bssid, reinterpret_cast<const char*>(record->ssid));
  WiFiNetwork network(record, id);

  Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Discovered, network, CaptivePortal::BroadcastMessageBIN);
}

void OpenShock::EventHandlers::WiFiScan::Init() {
  s_scanStatusChangedHandle     = WiFiScanManager::RegisterStatusChangedHandler(_scanStatusChangedHandler);
  s_scanNetworkDiscoveredHandle = WiFiScanManager::RegisterNetworkDiscoveryHandler(_scanNetworkDiscoveredHandler);
}

void OpenShock::EventHandlers::WiFiScan::Deinit() {
  WiFiScanManager::UnregisterStatusChangedHandler(s_scanStatusChangedHandle);
  WiFiScanManager::UnregisterNetworkDiscoveredHandler(s_scanNetworkDiscoveredHandle);
}
