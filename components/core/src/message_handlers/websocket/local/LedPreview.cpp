#include "message_handlers/impl/WSLocal.h"

const char* const TAG = "LocalMessageHandlers";

#include "Logging.h"
#include "visual/VisualStateManager.h"

#include <optional>
#include <utility>

namespace Schemas = OpenShock::Serialization::Local;
namespace Vsm     = OpenShock::VisualStateManager;

// The handlers cast between these by value
static_assert(std::to_underlying(Vsm::LedPattern::WiFiDisconnected) == std::to_underlying(Schemas::LedPreviewPattern::WifiDisconnected));
static_assert(std::to_underlying(Vsm::LedPattern::WiFiScanning) == std::to_underlying(Schemas::LedPreviewPattern::WifiScanning));
static_assert(std::to_underlying(Vsm::LedPattern::NoBackend) == std::to_underlying(Schemas::LedPreviewPattern::NoBackend));
static_assert(std::to_underlying(Vsm::LedPattern::Connected) == std::to_underlying(Schemas::LedPreviewPattern::Connected));
static_assert(std::to_underlying(Vsm::LedPattern::EStopped) == std::to_underlying(Schemas::LedPreviewPattern::EStopped));
static_assert(std::to_underlying(Vsm::LedPattern::EStopClearing) == std::to_underlying(Schemas::LedPreviewPattern::EStopClearing));
static_assert(std::to_underlying(Vsm::LedPattern::EStopAwaitingRelease) == std::to_underlying(Schemas::LedPreviewPattern::EStopAwaitingRelease));
static_assert(std::to_underlying(Vsm::LedPattern::CriticalError) == std::to_underlying(Schemas::LedPreviewPattern::CriticalError));

void OpenShock::MessageHandlers::Local::_Private::HandleLedPreviewPatternMessage(uint8_t socketId, const Schemas::LocalToHubMessage* msg)
{
  (void)socketId;

  auto preview = msg->payload_as_LedPreviewPatternMessage();
  if (preview == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as LedPreviewPatternMessage");
    return;
  }

  // The enum comes off the wire: the verifier does not check its range
  auto pattern = preview->pattern();
  if (pattern < Schemas::LedPreviewPattern::MIN || pattern > Schemas::LedPreviewPattern::MAX) {
    OS_LOGW(TAG, "Unknown LED preview pattern %u", static_cast<unsigned>(std::to_underlying(pattern)));
    return;
  }

  Vsm::PreviewPattern(static_cast<Vsm::LedPattern>(std::to_underlying(pattern)));
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
  switch (preview->target()) {
    case Schemas::LedTarget::All:
      break;
    case Schemas::LedTarget::Mono:
      only = LedType::Mono;
      break;
    case Schemas::LedTarget::Rgb:
      only = LedType::Rgb;
      break;
    default:
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
