#include "message_handlers/impl/WSLocal.h"

const char* const TAG = "LocalMessageHandlers";

#include "Logging.h"
#include "visual/VisualStateManager.h"

#include <optional>
#include <utility>

namespace Schemas = OpenShock::Serialization::Local;
namespace Vsm     = OpenShock::VisualStateManager;
using OpenShock::LedType;

// The conversions are exhaustive switches with no default on purpose: -Wswitch (IDF builds with -Werror) fails the
// build when the schema gains a value that is not handled here. The value still comes off the wire unchecked (the
// verifier does not check an enum's range), so anything else falls out of the switch as unknown.

static std::optional<Vsm::LedPattern> toLedPattern(Schemas::LedPreviewPattern pattern)
{
  switch (pattern) {
    case Schemas::LedPreviewPattern::WifiDisconnected:
      return Vsm::LedPattern::WiFiDisconnected;
    case Schemas::LedPreviewPattern::WifiScanning:
      return Vsm::LedPattern::WiFiScanning;
    case Schemas::LedPreviewPattern::NoBackend:
      return Vsm::LedPattern::NoBackend;
    case Schemas::LedPreviewPattern::Connected:
      return Vsm::LedPattern::Connected;
    case Schemas::LedPreviewPattern::EStopped:
      return Vsm::LedPattern::EStopped;
    case Schemas::LedPreviewPattern::EStopClearing:
      return Vsm::LedPattern::EStopClearing;
    case Schemas::LedPreviewPattern::EStopAwaitingRelease:
      return Vsm::LedPattern::EStopAwaitingRelease;
    case Schemas::LedPreviewPattern::CriticalError:
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

void OpenShock::MessageHandlers::Local::_Private::HandleLedPreviewPatternMessage(uint8_t socketId, const Schemas::LocalToHubMessage* msg)
{
  (void)socketId;

  auto preview = msg->payload_as_LedPreviewPatternMessage();
  if (preview == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as LedPreviewPatternMessage");
    return;
  }

  auto pattern = toLedPattern(preview->pattern());
  if (!pattern) {
    OS_LOGW(TAG, "Unknown LED preview pattern %u", static_cast<unsigned>(std::to_underlying(preview->pattern())));
    return;
  }

  Vsm::PreviewPattern(*pattern);
}

void OpenShock::MessageHandlers::Local::_Private::HandleLedPreviewColorMessage(uint8_t socketId, const Schemas::LocalToHubMessage* msg)
{
  (void)socketId;

  auto preview = msg->payload_as_LedPreviewColorMessage();
  if (preview == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as LedPreviewColorMessage");
    return;
  }

  std::optional<LedType> only;
  if (!toLedTarget(preview->target(), only)) {
    OS_LOGW(TAG, "Unknown LED preview target %u", static_cast<unsigned>(std::to_underlying(preview->target())));
    return;
  }

  auto brightness = preview->brightness();
  Vsm::PreviewColor(preview->red(), preview->green(), preview->blue(), only, brightness.has_value() ? std::optional<uint16_t>(*brightness) : std::nullopt);
}

void OpenShock::MessageHandlers::Local::_Private::HandleLedPreviewStopMessage(uint8_t socketId, const Schemas::LocalToHubMessage* msg)
{
  (void)socketId;
  (void)msg;

  Vsm::StopPreview();
}
