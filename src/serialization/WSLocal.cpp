#include "serialization/WSLocal.h"

#include "Utils/HexUtils.h"
#include "wifi/WiFiNetwork.h"

#include "serialization/_fbs/DeviceToLocalMessage_generated.h"

using namespace OpenShock::Serialization;

typedef OpenShock::Serialization::Types::WifiAuthMode WiFiAuthMode;

constexpr WiFiAuthMode GetWiFiAuthModeEnum(wifi_auth_mode_t authMode) {
  switch (authMode) {
    case wifi_auth_mode_t::WIFI_AUTH_OPEN:
      return WiFiAuthMode::Open;
    case wifi_auth_mode_t::WIFI_AUTH_WEP:
      return WiFiAuthMode::WEP;
    case wifi_auth_mode_t::WIFI_AUTH_WPA_PSK:
      return WiFiAuthMode::WPA_PSK;
    case wifi_auth_mode_t::WIFI_AUTH_WPA2_PSK:
      return WiFiAuthMode::WPA2_PSK;
    case wifi_auth_mode_t::WIFI_AUTH_WPA_WPA2_PSK:
      return WiFiAuthMode::WPA_WPA2_PSK;
    case wifi_auth_mode_t::WIFI_AUTH_WPA2_ENTERPRISE:
      return WiFiAuthMode::WPA2_ENTERPRISE;
    case wifi_auth_mode_t::WIFI_AUTH_WPA3_PSK:
      return WiFiAuthMode::WPA3_PSK;
    case wifi_auth_mode_t::WIFI_AUTH_WPA2_WPA3_PSK:
      return WiFiAuthMode::WPA2_WPA3_PSK;
    case wifi_auth_mode_t::WIFI_AUTH_WAPI_PSK:
      return WiFiAuthMode::WAPI_PSK;
    default:
      return WiFiAuthMode::UNKNOWN;
  }
}

flatbuffers::Offset<OpenShock::Serialization::Local::WifiNetwork> _createWiFiNetwork(flatbuffers::FlatBufferBuilder& builder, const OpenShock::WiFiNetwork& network) {
  auto bssid    = network.GetHexBSSID();
  auto authMode = GetWiFiAuthModeEnum(network.authMode);

  return Local::CreateWifiNetworkDirect(builder, network.ssid, bssid.data(), network.channel, network.rssi, authMode, network.IsSaved());
}

bool Local::SerializeErrorMessage(const char* message, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Local::CreateErrorMessage(builder, builder.CreateString(message));

  auto msg = Local::CreateDeviceToLocalMessage(builder, Local::DeviceToLocalMessagePayload::ErrorMessage, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  callback(span.data(), span.size());
}

bool Local::SerializeReadyMessage(const WiFiNetwork* connectedNetwork, bool gatewayPaired, std::uint8_t radioTxPin, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);

  flatbuffers::Offset<Serialization::Local::WifiNetwork> fbsNetwork = 0;

  if (connectedNetwork != nullptr) {
    fbsNetwork = _createWiFiNetwork(builder, *connectedNetwork);
  } else {
    fbsNetwork = 0;
  }

  auto readyMessageOffset = Serialization::Local::CreateReadyMessage(builder, true, fbsNetwork, gatewayPaired, radioTxPin);

  auto msg = Serialization::Local::CreateDeviceToLocalMessage(builder, Serialization::Local::DeviceToLocalMessagePayload::ReadyMessage, readyMessageOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Local::SerializeWiFiScanStatusChangedEvent(OpenShock::WiFiScanStatus status, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(32);  // TODO: Profile this and adjust the size accordingly

  Serialization::Local::WifiScanStatusMessage scanStatus(status);
  auto scanStatusOffset = builder.CreateStruct(scanStatus);

  auto msg = Serialization::Local::CreateDeviceToLocalMessage(builder, Serialization::Local::DeviceToLocalMessagePayload::WifiScanStatusMessage, scanStatusOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Local::SerializeWiFiScanNetworkDiscoveredEvent(const wifi_ap_record_t* ap_record, bool isSaved, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(512);  // TODO: Profile this and adjust the size accordingly

  WiFiAuthMode authMode = GetWiFiAuthModeEnum(ap_record->authmode);

  const char* ssid = reinterpret_cast<const char*>(ap_record->ssid);

  auto ssidOffset = builder.CreateString(ssid);

  char bssid[18];
  HexUtils::ToHexMac<6>(ap_record->bssid, bssid);
  auto bssidOffset = builder.CreateString(bssid, 17);

  auto wifiNetworkOffset = Serialization::Local::CreateWifiNetwork(builder, ssidOffset, bssidOffset, ap_record->rssi, ap_record->primary, authMode, isSaved);

  auto disoveryEventOffset = Serialization::Local::CreateWifiNetworkDiscoveredEvent(builder, wifiNetworkOffset);

  auto msg = Serialization::Local::CreateDeviceToLocalMessage(builder, Serialization::Local::DeviceToLocalMessagePayload::WifiNetworkDiscoveredEvent, disoveryEventOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Local::SerializeWiFiNetworkSavedEvent(const WiFiNetwork& network, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Local::CreateWifiNetworkSavedEvent(builder, _createWiFiNetwork(builder, network));

  auto msg = Local::CreateDeviceToLocalMessage(builder, Local::DeviceToLocalMessagePayload::WifiNetworkSavedEvent, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Local::SerializeWiFiNetworkLostEvent(const WiFiNetwork& network, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Local::CreateWifiNetworkLostEvent(builder, _createWiFiNetwork(builder, network));

  auto msg = Local::CreateDeviceToLocalMessage(builder, Local::DeviceToLocalMessagePayload::WifiNetworkLostEvent, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Local::SerializeWiFiNetworkUpdatedEvent(const WiFiNetwork& network, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Local::CreateWifiNetworkUpdatedEvent(builder, _createWiFiNetwork(builder, network));

  auto msg = Local::CreateDeviceToLocalMessage(builder, Local::DeviceToLocalMessagePayload::WifiNetworkUpdatedEvent, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Local::SerializeWiFiNetworkConnectedEvent(const WiFiNetwork& network, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Local::CreateWifiNetworkConnectedEvent(builder, _createWiFiNetwork(builder, network));

  auto msg = Local::CreateDeviceToLocalMessage(builder, Local::DeviceToLocalMessagePayload::WifiNetworkConnectedEvent, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Local::SerializeWiFiNetworkDiscoveredEvent(const WiFiNetwork& network, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Local::CreateWifiNetworkDiscoveredEvent(builder, _createWiFiNetwork(builder, network));

  auto msg = Local::CreateDeviceToLocalMessage(builder, Local::DeviceToLocalMessagePayload::WifiNetworkDiscoveredEvent, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}
