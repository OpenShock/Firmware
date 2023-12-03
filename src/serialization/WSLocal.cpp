#include "serialization/WSLocal.h"

#include "util/HexUtils.h"
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

flatbuffers::Offset<OpenShock::Serialization::Types::WifiNetwork> _createWiFiNetwork(flatbuffers::FlatBufferBuilder& builder, const OpenShock::WiFiNetwork& network) {
  auto bssid    = network.GetHexBSSID();
  auto authMode = GetWiFiAuthModeEnum(network.authMode);

  return Types::CreateWifiNetworkDirect(builder, network.ssid, bssid.data(), network.channel, network.rssi, authMode);
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

bool Local::SerializeReadyMessage(std::uint8_t radioTxPin, bool accountLinked, std::vector<std::string>& savedNetworkSSIDs, const WiFiNetwork* connectedNetwork, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);

  flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>> fbsSavedNetworkSSIDs = 0;

  std::vector<flatbuffers::Offset<flatbuffers::String>> fbsSavedNetworkSSIDsVec;
  fbsSavedNetworkSSIDsVec.reserve(savedNetworkSSIDs.size());

  for (auto& ssid : savedNetworkSSIDs) {
    fbsSavedNetworkSSIDsVec.push_back(builder.CreateString(ssid));
  }

  fbsSavedNetworkSSIDs = builder.CreateVector(fbsSavedNetworkSSIDsVec);

  flatbuffers::Offset<Serialization::Types::WifiNetwork> fbsNetwork = 0;

  if (connectedNetwork != nullptr) {
    fbsNetwork = _createWiFiNetwork(builder, *connectedNetwork);
  } else {
    fbsNetwork = 0;
  }

  auto readyMessageOffset = Serialization::Local::CreateReadyMessage(builder, true, radioTxPin, accountLinked, fbsSavedNetworkSSIDs, fbsNetwork);

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

bool Local::SerializeWiFiNetworkEvent(Types::WifiNetworkEventType eventType, const WiFiNetwork& network, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(256);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Local::CreateWifiNetworkEvent(builder, eventType, _createWiFiNetwork(builder, network));

  auto msg = Local::CreateDeviceToLocalMessage(builder, Local::DeviceToLocalMessagePayload::WifiNetworkEvent, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Local::SerializeWiFiIpAddressChangedEvent(const char* ipAddress, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(32);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Local::CreateWifiIpAddressChangedEvent(builder, builder.CreateString(ipAddress));

  auto msg = Local::CreateDeviceToLocalMessage(builder, Local::DeviceToLocalMessagePayload::WifiIpAddressChangedEvent, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Local::SerializeSavedNetworkAddedEvent(const char* ssid, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(32);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Local::CreateSavedNetworkAddedEvent(builder, builder.CreateString(ssid));

  auto msg = Local::CreateDeviceToLocalMessage(builder, Local::DeviceToLocalMessagePayload::SavedNetworkAddedEvent, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Local::SerializeSavedNetworkRemovedEvent(const char* ssid, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(32);  // TODO: Profile this and adjust the size accordingly

  auto wrapperOffset = Local::CreateSavedNetworkRemovedEvent(builder, builder.CreateString(ssid));

  auto msg = Local::CreateDeviceToLocalMessage(builder, Local::DeviceToLocalMessagePayload::SavedNetworkRemovedEvent, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Local::SerializeAccountLinkCommandResult(AccountLinkResultCode resultCode, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(32);  // TODO: Profile this and adjust the size accordingly

  Local::AccountLinkCommandResult accountLinkCommandResult(resultCode);

  auto wrapperOffset = builder.CreateStruct(accountLinkCommandResult);

  auto msg = Local::CreateDeviceToLocalMessage(builder, Local::DeviceToLocalMessagePayload::AccountLinkCommandResult, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}

bool Local::SerializeSetRfTxPinCommandResult(std::uint8_t pin, SetRfPinResultCode resultCode, Common::SerializationCallbackFn callback) {
  flatbuffers::FlatBufferBuilder builder(32);  // TODO: Profile this and adjust the size accordingly

  Local::SetRfTxPinCommandResult setRfTxPinCommandResult(pin, resultCode);

  auto wrapperOffset = builder.CreateStruct(setRfTxPinCommandResult);

  auto msg = Local::CreateDeviceToLocalMessage(builder, Local::DeviceToLocalMessagePayload::SetRfTxPinCommandResult, wrapperOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  return callback(span.data(), span.size());
}
