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
      , prerelease(std::string(prerelease))
      , build(std::string(build))
    {
    }

    bool operator==(const SemVer& other) const;
    inline bool operator!=(const SemVer& other) const { return !(*this == other); }
    bool operator<(const SemVer& other) const;
    inline bool operator<=(const SemVer& other) const { return *this < other || *this == other; }
    inline bool operator>(const SemVer& other) const { return !(*this <= other); }
    inline bool operator>=(const SemVer& other) const { return !(*this < other); }

    bool operator==(std::string_view other) const;
    inline bool operator!=(std::string_view other) const { return !(*this == other); }
    bool operator<(std::string_view other) const;
    inline bool operator<=(std::string_view other) const { return *this < other || *this == other; }
    inline bool operator>(std::string_view other) const { return !(*this <= other); }
    inline bool operator>=(std::string_view other) const { return !(*this < other); }

    bool isValid() const;

    std::string toString() const;
  };

  bool TryParseSemVer(std::string_view str, SemVer& out);
}  // namespace OpenShock
