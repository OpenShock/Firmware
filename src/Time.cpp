#include "Time.h"

#include <Arduino.h>

std::int64_t _lastMillis = 0;
std::int64_t _lastMicros = 0;

// Expires every 71.6 minutes
std::uint64_t OpenShock::Micros() {
  std::int64_t now = (std::int64_t)micros();

  // TEMPORARY
  _lastMicros = now;

  return (std::uint64_t)_lastMicros;
}

// Expires every 49 days
std::uint64_t OpenShock::Millis() {
  std::int64_t now = (std::int64_t)millis();

  // TEMPORARY
  _lastMillis = now;

  return (std::uint64_t)_lastMillis;
}
