#pragma once

#include "enums/StatusLed.h"

#include <cstdint>
#include <optional>

// Overrides: what a portal client puts on the status LEDs without saving it. There are two independent layers, each
// held by one client at a time and timed by the hub, so a client that goes quiet or away never leaves the LEDs stuck:
//
//  - Display: a previewed pattern or solid color, shown instead of the hub's state.
//  - Draft:   unsaved brightness, applied to whatever shows (the hub's state or a preview) until the owner saves it
//             (CommitLedDraft) or stops sending it.
//
// Every change to what the LEDs show, every override ending, and every failed request posts
// OPENSHOCK_EVENT_LED_STATUS_CHANGED with an LedStatusNotice; GetLedStatus() reads the state it reports.

namespace OpenShock::VisualStateManager {
  enum class LedPattern : uint8_t {
    WiFiDisconnected,
    WiFiScanning,
    NoBackend,  // Network up, gateway not connected
    Connected,
    EStopped,
    EStopClearing,
    EStopAwaitingRelease,
    CriticalError,
  };

  enum class LedOverrideLayer : uint8_t {
    Display,
    Draft,
  };

  enum class LedOverrideEnd : uint8_t {
    Expired,    // The owner sent nothing for it within its timeout
    Stopped,    // The owner ended it
    Replaced,   // Another client took the layer over
    OwnerLeft,  // The owner disconnected
    Committed,  // Saved (a draft only)
    LedTest,    // The LED test took over the LEDs
  };

  enum class LedRequestError : uint8_t {
    NoLed,           // The board has none of the LEDs the request is for
    LedTestRunning,  // The LED test holds the LEDs
    NotHeld,         // The client holds no override on that layer
    SaveFailed,      // The settings could not be saved
    InvalidValue,    // A value the hub doesn't know
  };

  enum class LedShowing : uint8_t {
    Live,     // The hub's state (LedStatus::pattern)
    Hidden,   // The hub's state, but the LED mode keeps the LEDs dark
    Pattern,  // A previewed pattern (LedStatus::pattern)
    Color,    // A previewed color
    Test,     // The LED test
  };

  // Display timeouts: a color ends this long after its owner's last one; a pattern shows about two of its cycles,
  // within these bounds. A draft ends this long after its owner's last one.
  inline constexpr uint16_t kColorPreviewTimeoutMs    = 2'000;
  inline constexpr uint16_t kPatternPreviewMinMs      = 3'000;
  inline constexpr uint16_t kPatternPreviewMaxMs      = 12'000;
  inline constexpr uint16_t kDraftBrightnessTimeoutMs = 10'000;

  struct LedOverrideInfo {
    uint8_t owner;
    uint16_t timeoutMs;

    bool operator==(const LedOverrideInfo&) const = default;
  };

  struct LedStatus {
    LedShowing showing;
    LedPattern pattern;  // For Live and Pattern
    std::optional<LedOverrideInfo> display;
    std::optional<LedOverrideInfo> draft;

    bool operator==(const LedStatus&) const = default;
  };

  // The event data of OPENSHOCK_EVENT_LED_STATUS_CHANGED: what the event reports beyond the current status
  struct LedStatusNotice {
    enum class Kind : uint8_t {
      StatusChanged,  // Only the status (GetLedStatus) changed
      Ended,          // An override ended: layer, reason, owner, seq
      Failed,         // A request failed: error, owner, seq
    };
    Kind kind;
    LedOverrideLayer layer;
    LedOverrideEnd reason;
    LedRequestError error;
    uint8_t owner;
    uint16_t seq;
  };

  /// @brief Brightness for each LED, 16-bit (0-65535); unset for an LED the draft leaves at its saved values.
  struct LedDraftBrightness {
    struct Pair {
      uint16_t critical;
      uint16_t normal;
      bool operator==(const Pair&) const = default;
    };
    std::optional<Pair> mono;
    std::optional<Pair> rgb;
    bool operator==(const LedDraftBrightness&) const = default;
  };

  [[nodiscard]] LedStatus GetLedStatus();

  /// @brief Shows `pattern` whatever the LED mode, for about two of its cycles.
  void PreviewLedPattern(uint8_t owner, uint16_t seq, LedPattern pattern);
  /// @brief Shows a solid color: the RGB LED in `red`/`green`/`blue` (16 bits per channel, 0-65535; the driver dithers
  ///        what the LED's 8 bits can't show), the mono LED on unless the color is black. Ends kColorPreviewTimeoutMs
  ///        after the last call.
  /// @param only Light only this LED; the other stays dark. All LEDs when unset.
  /// @param signal Which brightness it shows at: the draft's if one is held, else the saved one.
  void PreviewLedColor(uint8_t owner, uint16_t seq, uint16_t red, uint16_t green, uint16_t blue, std::optional<LedType> only, LedSignal signal);
  /// @brief Replaces `owner`'s draft brightness. Ends kDraftBrightnessTimeoutMs after the last call.
  void SetLedDraft(uint8_t owner, uint16_t seq, const LedDraftBrightness& draft);
  /// @brief Ends `owner`'s override on `layer`.
  void StopLedOverride(uint8_t owner, uint16_t seq, LedOverrideLayer layer);
  /// @brief Saves `owner`'s draft to the `led` config section.
  void CommitLedDraft(uint8_t owner, uint16_t seq);
  /// @brief Saves the LED mode, reporting a failure to `owner`.
  void SetLedModeFor(uint8_t owner, uint16_t seq, LedMode mode);
  /// @brief Reports a request the caller could not even parse (an unknown enum value) as failed.
  void ReportLedRequestFailed(uint8_t owner, uint16_t seq, LedRequestError error);
  /// @brief Ends every override `owner` holds, for a client that disconnected.
  void ReleaseLedOverrides(uint8_t owner);
}  // namespace OpenShock::VisualStateManager
