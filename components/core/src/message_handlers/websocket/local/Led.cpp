#include "message_handlers/impl/WSLocal.h"

const char* const TAG = "LocalMessageHandlers";

#include "Logging.h"
#include "visual/VisualStateManager.h"

#include <optional>
#include <utility>

namespace Schemas = OpenShock::Serialization::Local;
namespace Types   = OpenShock::Serialization::Types;
namespace Vsm     = OpenShock::VisualStateManager;
using OpenShock::LedMode;
using OpenShock::LedSignal;
using OpenShock::LedType;

// The status LED requests. Each ends up in VisualStateManager, which reports its outcome to every portal
// (LedStatusEvent) under the sender's socket id; one this handler can't parse is reported the same way.
//
// The conversions are exhaustive switches with no default on purpose: -Wswitch (IDF builds with -Werror) fails the
// build when the schema gains a value that is not handled here. The value still comes off the wire unchecked (the
// verifier does not check an enum's range), so anything else falls out of the switch as unknown.

static std::optional<Vsm::LedPattern> toLedPattern(Types::LedPattern pattern)
{
  switch (pattern) {
    case Types::LedPattern::WifiDisconnected:
      return Vsm::LedPattern::WiFiDisconnected;
    case Types::LedPattern::WifiScanning:
      return Vsm::LedPattern::WiFiScanning;
    case Types::LedPattern::NoBackend:
      return Vsm::LedPattern::NoBackend;
    case Types::LedPattern::Connected:
      return Vsm::LedPattern::Connected;
    case Types::LedPattern::EStopped:
      return Vsm::LedPattern::EStopped;
    case Types::LedPattern::EStopClearing:
      return Vsm::LedPattern::EStopClearing;
    case Types::LedPattern::EStopAwaitingRelease:
      return Vsm::LedPattern::EStopAwaitingRelease;
    case Types::LedPattern::CriticalError:
      return Vsm::LedPattern::CriticalError;
  }
  return std::nullopt;
}

// The LEDs a color preview lights: nullopt for every LED, or one type
static bool toLedTarget(Schemas::LedTarget target, std::optional<LedType>& only)
{
  switch (target) {
    case Schemas::LedTarget::All:
      only = std::nullopt;
      return true;
    case Schemas::LedTarget::Mono:
      only = LedType::Mono;
      return true;
    case Schemas::LedTarget::Rgb:
      only = LedType::Rgb;
      return true;
  }
  return false;
}

static std::optional<LedSignal> toLedSignal(Schemas::LedSignal signal)
{
  switch (signal) {
    case Schemas::LedSignal::Normal:
      return LedSignal::Normal;
    case Schemas::LedSignal::Critical:
      return LedSignal::Critical;
  }
  return std::nullopt;
}

static std::optional<Vsm::LedOverrideLayer> toLedOverrideLayer(Types::LedOverrideLayer layer)
{
  switch (layer) {
    case Types::LedOverrideLayer::Display:
      return Vsm::LedOverrideLayer::Display;
    case Types::LedOverrideLayer::Draft:
      return Vsm::LedOverrideLayer::Draft;
  }
  return std::nullopt;
}

static std::optional<LedMode> toLedMode(Types::LedMode mode)
{
  switch (mode) {
    case Types::LedMode::All:
      return LedMode::All;
    case Types::LedMode::CriticalOnly:
      return LedMode::CriticalOnly;
    case Types::LedMode::Off:
      return LedMode::Off;
  }
  return std::nullopt;
}

static void rejectUnknown(uint8_t socketId, uint16_t seq, const char* what, unsigned value)
{
  OS_LOGW(TAG, "Unknown LED %s %u", what, value);
  Vsm::ReportLedRequestFailed(socketId, seq, Vsm::LedRequestError::InvalidValue);
}

void OpenShock::MessageHandlers::Local::_Private::HandleLedPreviewPatternMessage(uint8_t socketId, const Schemas::LocalToHubMessage* msg)
{
  auto preview = msg->payload_as_LedPreviewPatternMessage();
  if (preview == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as LedPreviewPatternMessage");
    return;
  }

  auto pattern = toLedPattern(preview->pattern());
  if (!pattern) {
    return rejectUnknown(socketId, preview->seq(), "preview pattern", std::to_underlying(preview->pattern()));
  }

  Vsm::PreviewLedPattern(socketId, preview->seq(), *pattern);
}

void OpenShock::MessageHandlers::Local::_Private::HandleLedPreviewColorMessage(uint8_t socketId, const Schemas::LocalToHubMessage* msg)
{
  auto preview = msg->payload_as_LedPreviewColorMessage();
  if (preview == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as LedPreviewColorMessage");
    return;
  }

  std::optional<LedType> only;
  if (!toLedTarget(preview->target(), only)) {
    return rejectUnknown(socketId, preview->seq(), "preview target", std::to_underlying(preview->target()));
  }
  auto signal = toLedSignal(preview->signal());
  if (!signal) {
    return rejectUnknown(socketId, preview->seq(), "preview signal", std::to_underlying(preview->signal()));
  }

  Vsm::PreviewLedColor(socketId, preview->seq(), preview->red(), preview->green(), preview->blue(), only, *signal);
}

void OpenShock::MessageHandlers::Local::_Private::HandleLedDraftMessage(uint8_t socketId, const Schemas::LocalToHubMessage* msg)
{
  auto draft = msg->payload_as_LedDraftMessage();
  if (draft == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as LedDraftMessage");
    return;
  }

  auto toPair = [](const Types::LedBrightness* brightness) -> std::optional<Vsm::LedDraftBrightness::Pair> {
    if (brightness == nullptr) {
      return std::nullopt;
    }
    return Vsm::LedDraftBrightness::Pair {.critical = brightness->critical(), .normal = brightness->normal()};
  };

  Vsm::SetLedDraft(socketId, draft->seq(), Vsm::LedDraftBrightness {.mono = toPair(draft->mono()), .rgb = toPair(draft->rgb())});
}

void OpenShock::MessageHandlers::Local::_Private::HandleLedOverrideStopMessage(uint8_t socketId, const Schemas::LocalToHubMessage* msg)
{
  auto stop = msg->payload_as_LedOverrideStopMessage();
  if (stop == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as LedOverrideStopMessage");
    return;
  }

  auto layer = toLedOverrideLayer(stop->layer());
  if (!layer) {
    return rejectUnknown(socketId, stop->seq(), "override layer", std::to_underlying(stop->layer()));
  }

  Vsm::StopLedOverride(socketId, stop->seq(), *layer);
}

void OpenShock::MessageHandlers::Local::_Private::HandleLedCommitDraftMessage(uint8_t socketId, const Schemas::LocalToHubMessage* msg)
{
  auto commit = msg->payload_as_LedCommitDraftMessage();
  if (commit == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as LedCommitDraftMessage");
    return;
  }

  Vsm::CommitLedDraft(socketId, commit->seq());
}

void OpenShock::MessageHandlers::Local::_Private::HandleLedSetModeMessage(uint8_t socketId, const Schemas::LocalToHubMessage* msg)
{
  auto setMode = msg->payload_as_LedSetModeMessage();
  if (setMode == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as LedSetModeMessage");
    return;
  }

  auto mode = toLedMode(setMode->mode());
  if (!mode) {
    return rejectUnknown(socketId, setMode->seq(), "mode", std::to_underlying(setMode->mode()));
  }

  Vsm::SetLedModeFor(socketId, setMode->seq(), *mode);
}
