#include "serialization/WSLocal.h"

#include "config/Config.h"
#include "Logging.h"
#include "util/HexUtils.h"
#include "wifi/WiFiNetwork.h"

#include "serialization/_fbs/DeviceToLocalMessage_generated.h"

const char* const TAG = "WSLocal";

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

flatbuffers::Offset<OpenShock::Serialization::Types::WifiNetwork> _createWiFiNetwork(flatbuffers::FlatBufferBuilder& builder, const OpenShock::WiFiNetwork& network) {
  auto bssid    = network.GetHexBSSID();
  auto authMode = GetWiFiAuthModeEnum(network.authMode);

  return Types::CreateWifiNetworkDirect(builder, network.ssid, bssid.data(), network.channel, network.rssi, authMode, network.IsSaved());
}

bool Local::SerializeErrorMessage(const char* message, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Local::CreateErrorMessage(builder, builder.CreateString(message));

  auto msg = Local::CreateDeviceToLocalMessage(builder, Local::DeviceToLocalMessagePayload::ErrorMessage, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  callback(span.data(), span.size());

  return true;
}

bool Local::SerializeReadyMessage(const WiFiNetwork* connectedNetwork, bool accountLinked, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);

  flatbuffers::Offset<Serialization::Types::WifiNetwork> fbsNetwork = 0;

  if (connectedNetwork != nullptr) {
    fbsNetwork = _createWiFiNetwork(builder, *connectedNetwork);
  } else {
    fbsNetwork = 0;
  }

  auto configOffset = OpenShock::Config::GetAsFlatBuffer(builder, false);
  if (configOffset.IsNull()) {
    ESP_LOGE(TAG, "Failed to serialize config");
    return false;
  }

  auto readyMessageOffset = Serialization::Local::CreateReadyMessage(builder, true, fbsNetwork, accountLinked, configOffset);

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

bool Local::SerializeWiFiNetworkEvent(Types::WifiNetworkEventType eventType, std::vector<WiFiNetwork> networks, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  std::vector<flatbuffers::Offset<Serialization::Types::WifiNetwork>> fbsNetworks;

  for (const auto& network : networks) {
    fbsNetworks.push_back(_createWiFiNetwork(builder, network));
  }

  auto wrapperOffset = Local::CreateWifiNetworkEvent(builder, eventType, builder.CreateVector(fbsNetworks));

  auto msg = Local::CreateDeviceToLocalMessage(builder, Local::DeviceToLocalMessagePayload::WifiNetworkEvent, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}
