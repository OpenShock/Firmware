#pragma once

#include "visual/LedOverrides.h"

#include <cstdint>
#include <optional>

// The two override layers and their deadlines, with no LED, timer or lock of its own: VisualStateManager holds it
// under its apply mutex, passes the time in, and turns what it returns into LED updates and status notices.

namespace OpenShock::VisualStateManager {
  struct LedDisplayContent {
    bool isColor;
    LedPattern pattern;         // Pattern preview
    uint16_t red, green, blue;  // Color preview: 16 bits per channel
    std::optional<LedType> only;
    LedSignal signal;
  };

  struct LedOverrideEnded {
    LedOverrideLayer layer;
    LedOverrideEnd reason;
    uint8_t owner;
    uint16_t seq;
  };

  class LedOverrideState {
  public:
    /// @brief Starts or refreshes the display override. Returns another client's override it replaced.
    std::optional<LedOverrideEnded> SetDisplay(uint8_t owner, uint16_t seq, const LedDisplayContent& content, uint16_t timeoutMs, int64_t nowUs) { return set(m_display, m_displayContent, LedOverrideLayer::Display, owner, seq, content, timeoutMs, nowUs); }
    /// @brief Starts or refreshes the draft. Returns another client's draft it replaced.
    std::optional<LedOverrideEnded> SetDraft(uint8_t owner, uint16_t seq, const LedDraftBrightness& draft, uint16_t timeoutMs, int64_t nowUs) { return set(m_draft, m_draftContent, LedOverrideLayer::Draft, owner, seq, draft, timeoutMs, nowUs); }

    [[nodiscard]] bool IsHeldBy(LedOverrideLayer layer, uint8_t owner) const
    {
      const Slot& slot = this->slot(layer);
      return slot.active && slot.owner == owner;
    }

    /// @brief Ends the override on `layer`, if any. `seq` replaces the owner's last seq in what is returned (the seq of
    ///        the request that ended it).
    std::optional<LedOverrideEnded> End(LedOverrideLayer layer, LedOverrideEnd reason, std::optional<uint16_t> seq = std::nullopt)
    {
      Slot& slot = this->slot(layer);
      if (!slot.active) {
        return std::nullopt;
      }
      slot.active = false;
      if (layer == LedOverrideLayer::Display) {
        m_displayContent.reset();
      } else {
        m_draftContent.reset();
      }
      return LedOverrideEnded {.layer = layer, .reason = reason, .owner = slot.owner, .seq = seq.value_or(slot.seq)};
    }

    /// @brief Ends each override whose deadline has passed, calling `onEnded` for it.
    template<typename F>
    void Expire(int64_t nowUs, F&& onEnded)
    {
      for (LedOverrideLayer layer : {LedOverrideLayer::Display, LedOverrideLayer::Draft}) {
        const Slot& slot = this->slot(layer);
        if (slot.active && slot.deadlineUs <= nowUs) {
          onEnded(*End(layer, LedOverrideEnd::Expired));
        }
      }
    }

    /// @brief The earliest deadline of the active overrides.
    [[nodiscard]] std::optional<int64_t> NextDeadlineUs() const
    {
      std::optional<int64_t> next;
      for (const Slot* slot : {&m_display, &m_draft}) {
        if (slot->active && (!next || slot->deadlineUs < *next)) {
          next = slot->deadlineUs;
        }
      }
      return next;
    }

    [[nodiscard]] std::optional<LedOverrideInfo> Info(LedOverrideLayer layer) const
    {
      const Slot& slot = this->slot(layer);
      if (!slot.active) {
        return std::nullopt;
      }
      return LedOverrideInfo {.owner = slot.owner, .timeoutMs = slot.timeoutMs};
    }

    [[nodiscard]] const std::optional<LedDisplayContent>& Display() const { return m_displayContent; }
    [[nodiscard]] const std::optional<LedDraftBrightness>& Draft() const { return m_draftContent; }

  private:
    struct Slot {
      bool active        = false;
      uint8_t owner      = 0;
      uint16_t seq       = 0;
      uint16_t timeoutMs = 0;
      int64_t deadlineUs = 0;
    };

    template<typename T>
    static std::optional<LedOverrideEnded> set(Slot& slot, std::optional<T>& content, LedOverrideLayer layer, uint8_t owner, uint16_t seq, const T& value, uint16_t timeoutMs, int64_t nowUs)
    {
      std::optional<LedOverrideEnded> replaced;
      if (slot.active && slot.owner != owner) {
        replaced = LedOverrideEnded {.layer = layer, .reason = LedOverrideEnd::Replaced, .owner = slot.owner, .seq = slot.seq};
      }
      slot    = Slot {.active = true, .owner = owner, .seq = seq, .timeoutMs = timeoutMs, .deadlineUs = nowUs + static_cast<int64_t>(timeoutMs) * 1000};
      content = value;
      return replaced;
    }

    Slot& slot(LedOverrideLayer layer) { return layer == LedOverrideLayer::Display ? m_display : m_draft; }
    const Slot& slot(LedOverrideLayer layer) const { return layer == LedOverrideLayer::Display ? m_display : m_draft; }

    Slot m_display;
    Slot m_draft;
    std::optional<LedDisplayContent> m_displayContent;
    std::optional<LedDraftBrightness> m_draftContent;
  };
}  // namespace OpenShock::VisualStateManager
