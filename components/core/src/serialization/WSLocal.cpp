#include <freertos/FreeRTOS.h>

#include "serialization/WSLocal.h"

const char* const TAG = "WSLocal";

#include "Chipset.h"
#include "estop/EStopConfig.h"
#include "Logging.h"
#include "OtaUpdateConfig.h"
#include "radio/RFConfig.h"
#include "util/HexUtils.h"
#include "visual/VisualStateManager.h"
#include "wifi/AccessPoint.h"
#include "wifi/WiFiConfig.h"
#include "wifi/WiFiNetwork.h"

#include "serialization/_fbs/HubToLocalMessage_generated.h"

#include <optional>
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

// Exhaustive with no default on purpose: -Werror=switch fails the build when OtaUpdateChannel gains a value that is not
// mapped here. Any other value (a caller can pass anything) falls out as nullopt.
static std::optional<Local::OtaChannel> toSchemaOtaChannel(OpenShock::OtaUpdateChannel channel)
{
  switch (channel) {
    case OpenShock::OtaUpdateChannel::Stable:
      return Local::OtaChannel::Stable;
    case OpenShock::OtaUpdateChannel::Beta:
      return Local::OtaChannel::Beta;
    case OpenShock::OtaUpdateChannel::Develop:
      return Local::OtaChannel::Develop;
  }
  return std::nullopt;
}

static flatbuffers::Offset<Local::OtaSettings> createOtaSettings(flatbuffers::FlatBufferBuilder& builder)
{
  const auto ota = OpenShock::Config::GetOtaUpdateConfig();

  auto cdnDomain = builder.CreateString(ota.cdnDomain);

  auto channel = toSchemaOtaChannel(ota.updateChannel);
  if (!channel) {
    // The hub follows no channel for one it doesn't know (its update check fails). Stable gives the portal a real
    // choice to show, and saving it from there repairs the config.
    OS_LOGE(TAG, "Unknown OTA update channel %u, reporting stable", static_cast<unsigned>(std::to_underlying(ota.updateChannel)));
  }

  return Local::CreateOtaSettings(builder, ota.isEnabled, cdnDomain, channel.value_or(Local::OtaChannel::Stable), ota.checkInterval, ota.allowBackendManagement, ota.requireManualApproval);
}

// Exhaustive with no default on purpose: -Werror=switch fails the build when LedMode gains a value that is not mapped
// here. Any other value (a caller can pass anything) falls out as nullopt.
static std::optional<Types::LedMode> toSchemaLedMode(OpenShock::LedMode mode)
{
  switch (mode) {
    case OpenShock::LedMode::All:
      return Types::LedMode::All;
    case OpenShock::LedMode::CriticalOnly:
      return Types::LedMode::CriticalOnly;
    case OpenShock::LedMode::Off:
      return Types::LedMode::Off;
  }
  return std::nullopt;
}

static flatbuffers::Offset<Local::LedSettings> createLedSettings(flatbuffers::FlatBufferBuilder& builder)
{
  namespace Vsm = OpenShock::VisualStateManager;
  using OpenShock::LedSignal;
  using OpenShock::LedType;

  // Only the LEDs the board has, so the portal shows no option for one that isn't there
  auto brightness = [&](LedType type) -> flatbuffers::Offset<Types::LedBrightness> {
    if (!Vsm::HasLed(type)) {
      return 0;
    }
    return Types::CreateLedBrightness(builder, Vsm::GetLedBrightness(type, LedSignal::Critical), Vsm::GetLedBrightness(type, LedSignal::Normal));
  };
  auto mono = brightness(LedType::Mono);
  auto rgb  = brightness(LedType::Rgb);

  const OpenShock::LedMode ledMode = Vsm::GetLedMode();
  auto mode                        = toSchemaLedMode(ledMode);
  if (!mode) {
    // The LEDs treat a mode they don't know like All (every state shown), so report that
    OS_LOGE(TAG, "Unknown LED mode %u, reporting all", static_cast<unsigned>(std::to_underlying(ledMode)));
  }

  return Local::CreateLedSettings(builder, mode.value_or(Types::LedMode::All), mono, rgb, Vsm::kRgbBrightnessWarn);
}

// The LED status conversions: exhaustive switches like the ones above, so a new value fails the build until it is
// mapped. A value outside the enum (none is produced) falls out as the first one.

static Types::LedPattern toSchemaLedPattern(OpenShock::VisualStateManager::LedPattern pattern)
{
  using OpenShock::VisualStateManager::LedPattern;
  switch (pattern) {
    case LedPattern::WiFiDisconnected:
      return Types::LedPattern::WifiDisconnected;
    case LedPattern::WiFiScanning:
      return Types::LedPattern::WifiScanning;
    case LedPattern::NoBackend:
      return Types::LedPattern::NoBackend;
    case LedPattern::Connected:
      return Types::LedPattern::Connected;
    case LedPattern::EStopped:
      return Types::LedPattern::EStopped;
    case LedPattern::EStopClearing:
      return Types::LedPattern::EStopClearing;
    case LedPattern::EStopAwaitingRelease:
      return Types::LedPattern::EStopAwaitingRelease;
    case LedPattern::CriticalError:
      return Types::LedPattern::CriticalError;
  }
  return Types::LedPattern::WifiDisconnected;
}

static Local::LedShowing toSchemaLedShowing(OpenShock::VisualStateManager::LedShowing showing)
{
  using OpenShock::VisualStateManager::LedShowing;
  switch (showing) {
    case LedShowing::Live:
      return Local::LedShowing::Live;
    case LedShowing::Hidden:
      return Local::LedShowing::Hidden;
    case LedShowing::Pattern:
      return Local::LedShowing::Pattern;
    case LedShowing::Color:
      return Local::LedShowing::Color;
    case LedShowing::Test:
      return Local::LedShowing::Test;
  }
  return Local::LedShowing::Live;
}

static Types::LedOverrideLayer toSchemaLedOverrideLayer(OpenShock::VisualStateManager::LedOverrideLayer layer)
{
  using OpenShock::VisualStateManager::LedOverrideLayer;
  switch (layer) {
    case LedOverrideLayer::Display:
      return Types::LedOverrideLayer::Display;
    case LedOverrideLayer::Draft:
      return Types::LedOverrideLayer::Draft;
  }
  return Types::LedOverrideLayer::Display;
}

static Local::LedOverrideEnd toSchemaLedOverrideEnd(OpenShock::VisualStateManager::LedOverrideEnd reason)
{
  using OpenShock::VisualStateManager::LedOverrideEnd;
  switch (reason) {
    case LedOverrideEnd::Expired:
      return Local::LedOverrideEnd::Expired;
    case LedOverrideEnd::Stopped:
      return Local::LedOverrideEnd::Stopped;
    case LedOverrideEnd::Replaced:
      return Local::LedOverrideEnd::Replaced;
    case LedOverrideEnd::OwnerLeft:
      return Local::LedOverrideEnd::OwnerLeft;
    case LedOverrideEnd::Committed:
      return Local::LedOverrideEnd::Committed;
    case LedOverrideEnd::LedTest:
      return Local::LedOverrideEnd::LedTest;
  }
  return Local::LedOverrideEnd::Expired;
}

static Local::LedRequestError toSchemaLedRequestError(OpenShock::VisualStateManager::LedRequestError error)
{
  using OpenShock::VisualStateManager::LedRequestError;
  switch (error) {
    case LedRequestError::NoLed:
      return Local::LedRequestError::NoLed;
    case LedRequestError::LedTestRunning:
      return Local::LedRequestError::LedTestRunning;
    case LedRequestError::NotHeld:
      return Local::LedRequestError::NotHeld;
    case LedRequestError::SaveFailed:
      return Local::LedRequestError::SaveFailed;
    case LedRequestError::InvalidValue:
      return Local::LedRequestError::InvalidValue;
  }
  return Local::LedRequestError::InvalidValue;
}

static flatbuffers::Offset<Local::LedStatus> createLedStatus(flatbuffers::FlatBufferBuilder& builder)
{
  const auto status = OpenShock::VisualStateManager::GetLedStatus();

  auto layer = [&](const std::optional<OpenShock::VisualStateManager::LedOverrideInfo>& info) -> flatbuffers::Offset<Local::LedOverride> {
    if (!info.has_value()) {
      return 0;
    }
    return Local::CreateLedOverride(builder, info->owner, info->timeoutMs);
  };
  auto display = layer(status.display);
  auto draft   = layer(status.draft);

  return Local::CreateLedStatus(builder, toSchemaLedShowing(status.showing), toSchemaLedPattern(status.pattern), display, draft);
}

// The wire enum is the core one
static_assert(static_cast<uint8_t>(Local::AccessPointProtection::Open) == static_cast<uint8_t>(OpenShock::AccessPoint::Protection::Open));
static_assert(static_cast<uint8_t>(Local::AccessPointProtection::DefaultPassword) == static_cast<uint8_t>(OpenShock::AccessPoint::Protection::DefaultPassword));
static_assert(static_cast<uint8_t>(Local::AccessPointProtection::CustomPassword) == static_cast<uint8_t>(OpenShock::AccessPoint::Protection::CustomPassword));

// The access point's name and whether it is protected; never its password
static flatbuffers::Offset<Local::AccessPointSettings> createAccessPointSettings(flatbuffers::FlatBufferBuilder& builder)
{
  const std::string ssid     = OpenShock::Config::GetWiFiAccessPointSSID();
  const std::string password = OpenShock::Config::GetWiFiAccessPointPassword();

  // Empty for the default name, which default_ssid carries with the MAC
  auto ssidOffset    = builder.CreateString(OpenShock::AccessPoint::IsDefaultSSID(ssid) ? std::string_view() : std::string_view(ssid));
  auto defaultOffset = builder.CreateString(OpenShock::AccessPoint::DefaultSSID());
  auto protection    = static_cast<Local::AccessPointProtection>(OpenShock::AccessPoint::ProtectionOf(password));
  return Local::CreateAccessPointSettings(builder, ssidOffset, defaultOffset, protection);
}

bool Local::SerializeErrorMessage(std::string_view message, Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(64 + message.length());

  auto wrapperOffset = Local::CreateErrorMessage(builder, builder.CreateString(message.data(), message.length()));

  return finishMessage(builder, Local::HubToLocalMessagePayload::ErrorMessage, wrapperOffset, callback);
}

bool Local::SerializeReadyMessage(const WiFiNetwork* connectedNetwork, bool accountLinked, uint8_t clientId, Common::SerializationCallbackFn callback)
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
  OpenShock::Config::ForEachWiFiSsid([&](std::string_view ssid) { savedSsids.push_back(builder.CreateString(ssid.data(), ssid.size())); });
  auto savedSsidsOffset = builder.CreateVector(savedSsids);

  auto otaOffset         = createOtaSettings(builder);
  auto accessPointOffset = createAccessPointSettings(builder);
  auto ledOffset         = createLedSettings(builder);
  auto ledStatusOffset   = createLedStatus(builder);

  const auto rfTxPin = static_cast<int8_t>(OpenShock::Config::GetRFConfigTxPin());
  const auto estop   = OpenShock::Config::GetEStopConfig();

  auto readyMessageOffset = Serialization::Local::CreateReadyMessage(
    builder, true, fbsNetwork, accountLinked, inputPinsOffset, outputPinsOffset, false, rfTxPin, estop.enabled, static_cast<int8_t>(estop.gpioPin), savedSsidsOffset, otaOffset, accessPointOffset, ledOffset, ledStatusOffset, clientId
  );

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

bool Local::SerializeLedSettingsChangedEvent(Common::SerializationCallbackFn callback)
{
  flatbuffers::FlatBufferBuilder builder(128);

  auto ledOffset   = createLedSettings(builder);
  auto eventOffset = Local::CreateLedSettingsChangedEvent(builder, ledOffset);
  return finishMessage(builder, Local::HubToLocalMessagePayload::LedSettingsChangedEvent, eventOffset, callback);
}

bool Local::SerializeLedStatusEvent(const VisualStateManager::LedStatusNotice& notice, Common::SerializationCallbackFn callback)
{
  using Kind = VisualStateManager::LedStatusNotice::Kind;

  flatbuffers::FlatBufferBuilder builder(128);

  auto statusOffset = createLedStatus(builder);

  flatbuffers::Offset<Local::LedOverrideEnded> endedOffset  = 0;
  flatbuffers::Offset<Local::LedRequestFailed> failedOffset = 0;
  switch (notice.kind) {
    case Kind::StatusChanged:
      break;
    case Kind::Ended:
      endedOffset = Local::CreateLedOverrideEnded(builder, toSchemaLedOverrideLayer(notice.layer), toSchemaLedOverrideEnd(notice.reason), notice.owner, notice.seq);
      break;
    case Kind::Failed:
      failedOffset = Local::CreateLedRequestFailed(builder, notice.owner, notice.seq, toSchemaLedRequestError(notice.error));
      break;
  }

  auto eventOffset = Local::CreateLedStatusEvent(builder, statusOffset, endedOffset, failedOffset);
  return finishMessage(builder, Local::HubToLocalMessagePayload::LedStatusEvent, eventOffset, callback);
}
