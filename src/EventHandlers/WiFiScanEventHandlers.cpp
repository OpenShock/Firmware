#include "EventHandlers/WiFiScanEventHandlers.h"

#include "CaptivePortal.h"
#include "Mappers/EspWiFiTypesMapper.h"
#include "Utils/HexUtils.h"
#include "WiFiManager.h"
#include "WiFiScanManager.h"

#include "fbs/DeviceToLocalMessage_generated.h"

const char* const TAG = "WiFiScanEventHandlers";

static std::uint64_t s_scanStatusChangedHandle    = 0;
static std::uint64_t s_scanNetworkDiscoveryHandle = 0;

using namespace OpenShock;

void _scanStatusChangedHandler(OpenShock::WifiScanStatus status) {
  flatbuffers::FlatBufferBuilder builder(32);  // TODO: Profile this and adjust the size accordingly

  Serialization::Local::WifiScanStatusMessage scanStatus(status);
  auto scanStatusOffset = builder.CreateStruct(scanStatus);

  auto msg = Serialization::Local::CreateDeviceToLocalMessage(builder, Serialization::Local::DeviceToLocalMessagePayload::WifiScanStatusMessage, scanStatusOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  CaptivePortal::BroadcastMessageBIN(span.data(), span.size());
}

void _scanNetworkDiscoveryHandler(const wifi_ap_record_t* record) {
  flatbuffers::FlatBufferBuilder builder(512);  // TODO: Profile this and adjust the size accordingly

  OpenShock::WifiAuthMode authMode = Mappers::GetWiFiAuthModeEnum(record->authmode);

  const char* ssid = reinterpret_cast<const char*>(record->ssid);

  auto ssidOffset = builder.CreateString(ssid);

  char bssid[18];
  HexUtils::ToHexMac<6>(record->bssid, bssid);
  auto bssidOffset = builder.CreateString(bssid, 17);

  auto wifiNetworkOffset = Serialization::Local::CreateWifiNetwork(builder, ssidOffset, bssidOffset, record->rssi, record->primary, authMode, WiFiManager::IsSaved(ssid, record->bssid));

  auto disoveryEventOffset = Serialization::Local::CreateWifiNetworkDiscoveredEvent(builder, wifiNetworkOffset);

  auto msg = Serialization::Local::CreateDeviceToLocalMessage(builder, Serialization::Local::DeviceToLocalMessagePayload::WifiNetworkDiscoveredEvent, disoveryEventOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  CaptivePortal::BroadcastMessageBIN(span.data(), span.size());
}

void OpenShock::EventHandlers::WiFiScanEventHandler::Init() {
  s_scanStatusChangedHandle    = WiFiScanManager::RegisterStatusChangedHandler(_scanStatusChangedHandler);
  s_scanNetworkDiscoveryHandle = WiFiScanManager::RegisterNetworkDiscoveryHandler(_scanNetworkDiscoveryHandler);
}

void OpenShock::EventHandlers::WiFiScanEventHandler::Deinit() {
  WiFiScanManager::UnregisterStatusChangedHandler(s_scanStatusChangedHandle);
  WiFiScanManager::UnregisterNetworkDiscoveryHandler(s_scanNetworkDiscoveryHandle);
}
