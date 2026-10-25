#pragma once

#include <sdkconfig.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Counts how often each path that drops work instead of blocking has fired since boot. Each of them also logs, many at
// a rate limit; these show how often, without reading the log. Read by the `diag` serial command. Only counted with
// CONFIG_FREERTOS_USE_TRACE_FACILITY (which `diag` needs anyway); otherwise Increment() is empty, and with `diag` not
// linked in nothing references the counters.
namespace OpenShock::Diagnostics {
  enum class Counter : uint8_t {
    RfQueueFull,           // RF command not queued: the transmitter's queue stayed full
    RfCommandsSuperseded,  // Queued RF command discarded because a newer Stop for its shocker overtook it
    RfTransmitFailures,    // RMT frame that didn't go out
    RfSequencesDropped,    // Pending RF sequences dropped after RMT kept failing
    KeepAliveEvictions,    // Shocker dropped from keep-alive tracking past the 16-shocker cap
    KeepAliveQueueFull,    // Keep-alive activity not recorded: its queue stayed full
    EventPostFailures,     // OpenShock event not posted to the default loop (its queue stayed full)
    PortalWsQueueFull,     // Captive portal WebSocket message dropped: the send queue was full
    PortalDrainDelayed,    // Captive portal WebSocket send delayed: the httpd work queue was full
    GatewaySendTimeouts,   // Gateway message dropped: the websocket handle stayed locked
    _Count,
  };

  inline constexpr std::array<std::string_view, static_cast<std::size_t>(Counter::_Count)> kCounterNames = {
    "RfQueueFull",
    "RfCommandsSuperseded",
    "RfTransmitFailures",
    "RfSequencesDropped",
    "KeepAliveEvictions",
    "KeepAliveQueueFull",
    "EventPostFailures",
    "PortalWsQueueFull",
    "PortalDrainDelayed",
    "GatewaySendTimeouts",
  };

  // One definition across the image (inline variable); 32-bit so the atomics are lock-free on every chip
  inline std::array<std::atomic<uint32_t>, static_cast<std::size_t>(Counter::_Count)> g_counters = {};

  /// @brief Safe from any task. Relaxed: a counter orders nothing.
  inline void Increment(Counter counter, uint32_t amount = 1)
  {
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    g_counters[static_cast<std::size_t>(counter)].fetch_add(amount, std::memory_order_relaxed);
#else
    (void)counter;
    (void)amount;
#endif
  }

  inline uint32_t Get(Counter counter)
  {
    return g_counters[static_cast<std::size_t>(counter)].load(std::memory_order_relaxed);
  }

  inline std::string_view Name(Counter counter)
  {
    return kCounterNames[static_cast<std::size_t>(counter)];
  }
}  // namespace OpenShock::Diagnostics
