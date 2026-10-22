#include "serialization/WSLocal.h"

const char* const TAG = "WSLocal";

#include "Chipset.h"
#include "config/Config.h"
#include "config/WiFiAuthMode.h"
#include "Logging.h"
#include "util/HexUtils.h"
#include "wifi/WiFiNetwork.h"

#include "serialization/_fbs/HubToLocalMessage_generated.h"

using namespace OpenShock::Serialization;

// Wraps a payload in a HubToLocalMessage, finishes the buffer and hands it to the callback.
template<typename T>
static bool finishMessage(flatbuffers::FlatBufferBuilder& builder, Local::HubToLocalMessagePayload type, flatbuffers::Offset<T> payload, Common::SerializationCallbackFn callback)
{
  auto msg = Local::CreateHubToLocalMessage(builder, type, payload.Union());
  Local::FinishHubToLocalMessageBuffer(builder, msg);
  return callback(builder.GetBufferSpan());
}

static flatbuffers::Offset<OpenShock::Serialization::Types::WifiNetwork> createWiFiNetwork(flatbuffers::FlatBufferBuilder& builder, const OpenShock::WiFiNetwork& network)
{
  auto bssid    = network.GetHexBSSID();
  auto authMode = OpenShock::Config::ToFbsAuthMode(network.authMode);

  return Types::CreateWifiNetworkDirect(builder, network.ssid, bssid.data(), network.channel, network.rssi, authMode, network.IsSaved());
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

  auto configOffset = OpenShock::Config::GetAsFlatBuffer(builder, false);
  if (configOffset.IsNull()) {
    OS_LOGE(TAG, "Failed to serialize config");
    return false;
  }

  auto inputPins       = OpenShock::GetValidInputPinsVector();
  auto inputPinsOffset = builder.CreateVector(inputPins);

  auto outputPins       = OpenShock::GetValidOutputPinsVector();
  auto outputPinsOffset = builder.CreateVector(outputPins);

  auto readyMessageOffset = Serialization::Local::CreateReadyMessage(builder, true, fbsNetwork, accountLinked, configOffset, inputPinsOffset, outputPinsOffset);

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
