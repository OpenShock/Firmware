#include <freertos/FreeRTOS.h>

#include "serialization/WSLocal.h"

#include "Chipset.h"
#include "estop/EStopConfig.h"
#include "OtaUpdateConfig.h"
#include "radio/RFConfig.h"
#include "util/HexUtils.h"
#include "wifi/WiFiConfig.h"
#include "wifi/WiFiNetwork.h"

#include "serialization/_fbs/HubToLocalMessage_generated.h"

#include <utility>
#include <vector>

using namespace OpenShock::Serialization;

// Wraps a payload in a HubToLocalMessage, finishes the buffer and hands it to the callback.
template<typename T>
static bool finishMessage(flatbuffers::FlatBufferBuilder& builder, Local::HubToLocalMessagePayload type, flatbuffers::Offset<T> payload, Common::SerializationCallbackFn callback)
{
  auto msg = Local::CreateHubToLocalMessage(builder, type, payload.Union());
  Local::FinishHubToLocalMessageBuffer(builder, msg);
  return callback(builder.GetBufferSpan());
}

// Modes without a counterpart in the schema are UNKNOWN.
static constexpr Types::WifiAuthMode toFbsAuthMode(wifi_auth_mode_t mode)
{
  switch (mode) {
    case WIFI_AUTH_OPEN:
      return Types::WifiAuthMode::Open;
    case WIFI_AUTH_WEP:
      return Types::WifiAuthMode::WEP;
    case WIFI_AUTH_WPA_PSK:
      return Types::WifiAuthMode::WPA_PSK;
    case WIFI_AUTH_WPA2_PSK:
      return Types::WifiAuthMode::WPA2_PSK;
    case WIFI_AUTH_WPA_WPA2_PSK:
      return Types::WifiAuthMode::WPA_WPA2_PSK;
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return Types::WifiAuthMode::WPA2_ENTERPRISE;
    case WIFI_AUTH_WPA3_PSK:
      return Types::WifiAuthMode::WPA3_PSK;
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return Types::WifiAuthMode::WPA2_WPA3_PSK;
    case WIFI_AUTH_WAPI_PSK:
      return Types::WifiAuthMode::WAPI_PSK;
    default:
      return Types::WifiAuthMode::UNKNOWN;
  }
}

static flatbuffers::Offset<OpenShock::Serialization::Types::WifiNetwork> createWiFiNetwork(flatbuffers::FlatBufferBuilder& builder, const OpenShock::WiFiNetwork& network)
{
  auto bssid    = network.GetHexBSSID();
  auto authMode = toFbsAuthMode(network.authMode);

  return Types::CreateWifiNetworkDirect(builder, network.ssid, bssid.data(), network.channel, network.rssi, authMode, network.IsSaved());
}

// createOtaSettings casts between these by value
static_assert(std::to_underlying(OpenShock::OtaUpdateChannel::Stable) == std::to_underlying(Local::OtaChannel::Stable));
static_assert(std::to_underlying(OpenShock::OtaUpdateChannel::Beta) == std::to_underlying(Local::OtaChannel::Beta));
static_assert(std::to_underlying(OpenShock::OtaUpdateChannel::Develop) == std::to_underlying(Local::OtaChannel::Develop));

static flatbuffers::Offset<Local::OtaSettings> createOtaSettings(flatbuffers::FlatBufferBuilder& builder)
{
  const auto ota = OpenShock::Config::GetOtaUpdateConfig();

  auto cdnDomain = builder.CreateString(ota.cdnDomain);

  return Local::CreateOtaSettings(builder, ota.isEnabled, cdnDomain, static_cast<Local::OtaChannel>(ota.updateChannel), ota.checkInterval, ota.allowBackendManagement, ota.requireManualApproval);
}

bool Local::SerializeErrorMessage(std::string_view message, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(64 + message.length());

  auto wrapperOffset = Local::CreateErrorMessage(builder, builder.CreateString(message.data(), message.length()));

  return finishMessage(builder, Local::HubToLocalMessagePayload::ErrorMessage, wrapperOffset, callback);
}

bool Local::SerializeReadyMessage(const WiFiNetwork* connectedNetwork, bool accountLinked, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(768);

  flatbuffers::Offset<Serialization::Types::WifiNetwork> fbsNetwork = 0;

  if (connectedNetwork != nullptr) {
    fbsNetwork = createWiFiNetwork(builder, *connectedNetwork);
  }

  auto inputPins       = OpenShock::GetValidInputPinsVector();
  auto inputPinsOffset = builder.CreateVector(inputPins);

  auto outputPins       = OpenShock::GetValidOutputPinsVector();
  auto outputPinsOffset = builder.CreateVector(outputPins);

  // Only what the portal shows. The whole config has no wire form, and secrets never go into this message.
  std::vector<flatbuffers::Offset<flatbuffers::String>> savedSsids;
  for (const auto& credentials : OpenShock::Config::GetWiFiCredentials()) {
    savedSsids.push_back(builder.CreateString(credentials.ssid));
  }
  auto savedSsidsOffset = builder.CreateVector(savedSsids);

  auto otaOffset = createOtaSettings(builder);

  const auto rfTxPin = static_cast<int8_t>(OpenShock::Config::GetRFConfigTxPin());
  const auto estop   = OpenShock::Config::GetEStopConfig();

  auto readyMessageOffset = Serialization::Local::CreateReadyMessage(builder, true, fbsNetwork, accountLinked, inputPinsOffset, outputPinsOffset, false, rfTxPin, estop.enabled, static_cast<int8_t>(estop.gpioPin), savedSsidsOffset, otaOffset);

  return finishMessage(builder, Local::HubToLocalMessagePayload::ReadyMessage, readyMessageOffset, callback);
}

bool Local::SerializeWiFiScanStatusChangedEvent(OpenShock::WiFiScanStatus status, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(64);

  auto scanStatusOffset = Serialization::Local::CreateWifiScanStatusMessage(builder, status);

  return finishMessage(builder, Local::HubToLocalMessagePayload::WifiScanStatusMessage, scanStatusOffset, callback);
}

bool Local::SerializeWiFiNetworkEvent(Types::WifiNetworkEventType eventType, const WiFiNetwork& network, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(64);

  auto networkOffset = createWiFiNetwork(builder, network);

  auto wrapperOffset = Local::CreateWifiNetworkEvent(builder, eventType, builder.CreateVector(&networkOffset, 1));  // Resulting vector will have 1 element

  return finishMessage(builder, Local::HubToLocalMessagePayload::WifiNetworkEvent, wrapperOffset, callback);
}

bool Local::SerializeWiFiNetworksEvent(Types::WifiNetworkEventType eventType, const std::vector<WiFiNetwork>& networks, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(256);

  std::vector<flatbuffers::Offset<Serialization::Types::WifiNetwork>> fbsNetworks;
  fbsNetworks.reserve(networks.size());

  for (const auto& network : networks) {
    fbsNetworks.push_back(createWiFiNetwork(builder, network));
  }

  auto wrapperOffset = Local::CreateWifiNetworkEvent(builder, eventType, builder.CreateVector(fbsNetworks));

  return finishMessage(builder, Local::HubToLocalMessagePayload::WifiNetworkEvent, wrapperOffset, callback);
}

bool Local::SerializeWiFiGotIpEvent(const char* ip, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(64);

  auto ipOffset    = builder.CreateString(ip);
  auto eventOffset = Local::CreateWifiGotIpEvent(builder, ipOffset);
  return finishMessage(builder, Local::HubToLocalMessagePayload::WifiGotIpEvent, eventOffset, callback);
}

bool Local::SerializeAccountLinkStatusEvent(bool linked, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(64);

  auto eventOffset = Local::CreateAccountLinkStatusEvent(builder, linked);
  return finishMessage(builder, Local::HubToLocalMessagePayload::AccountLinkStatusEvent, eventOffset, callback);
}
