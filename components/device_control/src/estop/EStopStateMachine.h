#pragma once

#include "estop/EStopState.h"

#include <cstdint>

namespace OpenShock {
  /// @brief The EStop debounce + hold-to-clear state machine, free of FreeRTOS/GPIO so it can be host tested.
  ///        EStopManager's task feeds it one GPIO sample per tick (or a software trigger) and mirrors activatedAt() into
  ///        the atomic that IsEStopped() reads. activatedAt() is non-zero in every state except Idle.
  class EStopStateMachine {
  public:
    static constexpr int64_t kHoldToClearTime = 5000;
    static constexpr uint32_t kCheckCount     = 13;                                                // 65 ms at 200 Hz
    static constexpr uint16_t kCheckMask      = 0xFFFF >> ((sizeof(uint16_t) * 8) - kCheckCount);  // Mask to check only last kCheckCount bits within history

    // Grace period after deactivation (prevents immediate re-trigger on release bounce/EMI)
    static constexpr int64_t kRearmGraceTime = 250;  // tune as needed

    EStopState state() const noexcept { return m_state; }
    int64_t activatedAt() const noexcept { return m_activatedAt; }

    /// @brief Software trigger: forcibly set the E-Stop active. Keeps the original activation time if already active.
    void Trigger(int64_t now) noexcept
    {
      if (m_activatedAt == 0) {
        m_activatedAt = now;
      }

      m_state        = EStopState::Active;
      m_rearmBlocked = false;

      // Do not modify history/lastBtnState here; rely on physical button state
      // on subsequent samples.
    }

    /// @brief Feed one sample of the EStop input. The input is pulled up, so level 0 means pressed.
    void Sample(int level, int64_t now) noexcept
    {
      m_history = static_cast<uint16_t>((m_history << 1) | level);

      // Debounce:
      // If all recent bits are 1 -> fully released.
      // If any bit is 0 -> pressed (or bouncing toward pressed).
      bool btnState    = (m_history & kCheckMask) != kCheckMask;  // true == pressed
      bool pressedEdge = (btnState && !m_lastBtnState);
      m_lastBtnState   = btnState;

      switch (m_state) {
        case EStopState::Idle:
          // Rearm grace: after clearing, ignore presses for a short window.
          // After the window ends, require a released state before re-arming.
          if (m_rearmBlocked) {
            if (now < m_rearmAt) {
              // Still in grace window: ignore any press. Track input to avoid phantom edges later.
              break;
            }

            // Grace window ended: only re-arm once we see released.
            m_rearmBlocked = false;
          }

          if (btnState) {
            m_state       = EStopState::Active;
            m_activatedAt = now;
          }
          break;

        case EStopState::Active:
          // Once active, if the input gets pressed, start hold-to-clear timing.
          if (pressedEdge) {
            m_state         = EStopState::ActiveClearing;
            m_deactivatesAt = now + kHoldToClearTime;
          }
          break;

        case EStopState::ActiveClearing:
          if (!btnState) {  // released before hold time -> go back to Active
            m_state = EStopState::Active;
          } else if (now >= m_deactivatesAt) {
            // Hold complete -> now wait for release edge to fully clear
            m_state = EStopState::AwaitingRelease;
          }
          break;

        case EStopState::AwaitingRelease:
          if (!btnState) {  // fully released -> clear E-Stop
            m_state       = EStopState::Idle;
            m_activatedAt = 0;

            // Start grace period to prevent immediate re-trigger.
            m_rearmBlocked = true;
            m_rearmAt      = now + kRearmGraceTime;
          }
          break;

        default:
          // Should never happen
          break;
      }
    }

  private:
    EStopState m_state    = EStopState::Idle;
    int64_t m_activatedAt = 0;    // When == 0, EStop not active. When != 0, EStop is active.

    uint16_t m_history = 0xFFFF;  // Bit history of samples, 0 is pressed

    int64_t m_deactivatesAt = 0;

    // Rearm grace state
    int64_t m_rearmAt   = 0;
    bool m_rearmBlocked = false;

    // Debounced button state: true == pressed, false == released
    bool m_lastBtnState = false;
  };
}  // namespace OpenShock
