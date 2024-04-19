#pragma once

#include "StringView.h"

#include <cstdint>

namespace OpenShock {
  struct SemVer {
    std::uint16_t major;
    std::uint16_t minor;
    std::uint16_t patch;
    std::string prerelease;
    std::string build;

    SemVer() : major(0), minor(0), patch(0), prerelease(), build() {}
    SemVer(std::uint16_t major, std::uint16_t minor, std::uint16_t patch)
      : major(major), minor(minor), patch(patch), prerelease(), build()
    {}
    SemVer(std::uint16_t major, std::uint16_t minor, std::uint16_t patch, StringView prerelease, StringView build)
      : major(major), minor(minor), patch(patch), prerelease(prerelease.toString()), build(build.toString())
    {}

    bool operator==(const SemVer& other) const {
      return major == other.major && minor == other.minor && patch == other.patch && prerelease == other.prerelease && build == other.build;
    }
    bool operator!=(const SemVer& other) const {
      return !(*this == other);
    }
    bool operator<(const SemVer& other) const {
      if (major < other.major) {
        return true;
      }
      if (major > other.major) {
        return false;
      }

      if (minor < other.minor) {
        return true;
      }
      if (minor > other.minor) {
        return false;
      }

      if (patch < other.patch) {
        return true;
      }
      if (patch > other.patch) {
        return false;
      }

      if (prerelease < other.prerelease) {
        return true;
      }
      if (prerelease > other.prerelease) {
        return false;
      }

      return build < other.build;
    }
    bool operator<=(const SemVer& other) const {
      return *this < other || *this == other;
    }
    bool operator>(const SemVer& other) const {
      return !(*this <= other);
    }
    bool operator>=(const SemVer& other) const {
      return !(*this < other);
    }

    bool isValid() const;

    std::string toString() const;
  };

  bool TryParseSemVer(StringView str, SemVer& out);
} // namespace OpenShock
