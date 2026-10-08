#pragma once

#include <cstdint>

// Fake OpenShock::millis() for encoders with time-based state (T330 rolling counter).
namespace TestClock {
  extern int64_t NowMs;
}  // namespace TestClock
