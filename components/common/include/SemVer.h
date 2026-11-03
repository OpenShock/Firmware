#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// printf a SemVer without building a string: OS_LOGI(TAG, "version " SEMVER_FMT, SEMVER_ARG(v)). Like BSSID_FMT.
#define SEMVER_FMT    "%u.%u.%u%s%s%s%s"
#define SEMVER_ARG(v) static_cast<unsigned>((v).major), static_cast<unsigned>((v).minor), static_cast<unsigned>((v).patch), (v).prerelease.empty() ? "" : "-", (v).prerelease.c_str(), (v).build.empty() ? "" : "+", (v).build.c_str()

namespace OpenShock {
  struct SemVer {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    std::string prerelease;
    std::string build;

    inline SemVer()
      : major(0)
      , minor(0)
      , patch(0)
      , prerelease()
      , build()
    {
    }
    inline SemVer(uint16_t major, uint16_t minor, uint16_t patch)
      : major(major)
      , minor(minor)
      , patch(patch)
      , prerelease()
      , build()
    {
    }
    inline SemVer(uint16_t major, uint16_t minor, uint16_t patch, std::string_view prerelease, std::string_view build)
      : major(major)
      , minor(minor)
      , patch(patch)
      , prerelease(prerelease)
      , build(build)
    {
    }

    // == / != compare every field, build metadata included. The ordering operators compare precedence (semver 11),
    // which ignores build metadata, and are all derived from operator< so they stay consistent with each other.
    bool operator==(const SemVer& other) const;
    inline bool operator!=(const SemVer& other) const { return !(*this == other); }
    bool operator<(const SemVer& other) const;
    inline bool operator<=(const SemVer& other) const { return !(other < *this); }
    inline bool operator>(const SemVer& other) const { return other < *this; }
    inline bool operator>=(const SemVer& other) const { return !(*this < other); }

    // String comparisons parse `other` first; if it is not a valid semver every comparison except != is false.
    bool operator==(std::string_view other) const;
    inline bool operator!=(std::string_view other) const { return !(*this == other); }
    bool operator<(std::string_view other) const;
    bool operator<=(std::string_view other) const;
    bool operator>(std::string_view other) const;
    bool operator>=(std::string_view other) const;

    bool isValid() const;

    std::string toString() const;
  };

  [[nodiscard]] std::optional<SemVer> TryParseSemVer(std::string_view str);
}  // namespace OpenShock
