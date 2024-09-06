#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace OpenShock {
  struct SemVer {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    std::string prerelease;
    std::string build;

    SemVer() : major(0), minor(0), patch(0), prerelease(), build() {}
    SemVer(uint16_t major, uint16_t minor, uint16_t patch)
      : major(major), minor(minor), patch(patch), prerelease(), build()
    {}
    SemVer(uint16_t major, uint16_t minor, uint16_t patch, std::string_view prerelease, std::string_view build)
      : major(major), minor(minor), patch(patch), prerelease(std::string(prerelease)), build(std::string(build))
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

  bool TryParseSemVer(std::string_view str, SemVer& out);
}  // namespace OpenShock
