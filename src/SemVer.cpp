#include "SemVer.h"

#include "Logging.h"

const char* const TAG = "SemVer";

#include "Logging.h"
#include "util/StringUtils.h"

using namespace OpenShock;

constexpr bool _semverIsLetter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
constexpr bool _semverIsPositiveDigit(char c) {
  return c >= '1' && c <= '9';
}
constexpr bool _semverIsDigit(char c) {
  return c == '0' || _semverIsPositiveDigit(c);
}
constexpr bool _semverIsDigits(std::string_view str) {
  if (str.empty()) {
    return false;
  }

  for (auto c : str) {
    if (!_semverIsDigit(c)) {
      return false;
    }
  }

  return true;
}
constexpr bool _semverIsNonDigit(char c) {
  return _semverIsLetter(c) || c == '-';
}
constexpr bool _semverIsIdentifierChararacter(char c) {
  return _semverIsDigit(c) || _semverIsNonDigit(c);
}
constexpr bool _semverIsIdentifierChararacters(std::string_view str) {
  if (str.empty()) {
    return false;
  }

  for (auto c : str) {
    if (!_semverIsIdentifierChararacter(c)) {
      return false;
    }
  }

  return true;
}
constexpr bool _semverIsNumericIdentifier(std::string_view str) {
  if (str.empty()) {
    return false;
  }

  if (str.length() == 1) {
    return _semverIsDigit(str[0]);
  }

  return _semverIsPositiveDigit(str[0]) && _semverIsDigits(str.substr(1));
}
constexpr bool _semverIsAlphanumericIdentifier(std::string_view str) {
  if (str.empty()) {
    return false;
  }

  if (str.length() == 1) {
    return _semverIsNonDigit(str[0]);
  }

  std::size_t nonDigitPos = std::string_view::npos;
  for (std::size_t i = 0; i < str.length(); ++i) {
    if (_semverIsNonDigit(str[i])) {
      nonDigitPos = i;
      break;
    }
  }

  if (nonDigitPos == std::string_view::npos) {
    return false;
  }

  auto after = str.substr(nonDigitPos + 1);

  if (nonDigitPos == 0) {
    return _semverIsIdentifierChararacters(after);
  }

  auto before = str.substr(0, nonDigitPos);

  if (nonDigitPos == str.length() - 1) {
    return _semverIsIdentifierChararacters(before);
  }

  return _semverIsIdentifierChararacters(before) && _semverIsIdentifierChararacters(after);
}
constexpr bool _semverIsBuildIdentifier(std::string_view str) {
  return _semverIsAlphanumericIdentifier(str) || _semverIsDigits(str);
}
constexpr bool _semverIsPrereleaseIdentifier(std::string_view str) {
  return _semverIsAlphanumericIdentifier(str) || _semverIsNumericIdentifier(str);
}
constexpr bool _semverIsDotSeperatedBuildIdentifiers(std::string_view str) {
  if (str.empty()) {
    return false;
  }

  auto dotIdx = str.find('.');
  while (dotIdx != std::string_view::npos) {
    auto part = str.substr(0, dotIdx);
    if (!_semverIsBuildIdentifier(part)) {
      return false;
    }

    str    = str.substr(dotIdx + 1);
    dotIdx = str.find('.');
  }

  return _semverIsBuildIdentifier(str);
}
constexpr bool _semverIsDotSeperatedPreleaseIdentifiers(std::string_view str) {
  if (str.empty()) {
    return false;
  }

  auto dotIdx = str.find('.');
  while (dotIdx != std::string_view::npos) {
    auto part = str.substr(0, dotIdx);
    if (!_semverIsPrereleaseIdentifier(part)) {
      return false;
    }

    str    = str.substr(dotIdx + 1);
    dotIdx = str.find('.');
  }

  return _semverIsPrereleaseIdentifier(str);
}
const auto _semverIsPatch      = _semverIsNumericIdentifier;
const auto _semverIsMinor      = _semverIsNumericIdentifier;
const auto _semverIsMajor      = _semverIsNumericIdentifier;
const auto _semverIsPrerelease = _semverIsDotSeperatedPreleaseIdentifiers;
const auto _semverIsBuild      = _semverIsDotSeperatedBuildIdentifiers;
bool _semverIsVersionCore(std::string_view str) {
  if (str.empty()) {
    return false;
  }

  std::string_view parts[3];
  if (!OpenShock::TryStringSplit(str, '.', parts)) {
    return false;
  }

  return _semverIsMajor(parts[0]) && _semverIsMinor(parts[1]) && _semverIsPatch(parts[2]);
}
bool _semverIsSemver(std::string_view str) {
  if (str.empty()) {
    return false;
  }

  auto dashPos = str.find('-');
  auto plusPos = str.find('+');

  if (dashPos == std::string_view::npos && plusPos == std::string_view::npos) {
    return _semverIsVersionCore(str);
  }

  if (dashPos != std::string_view::npos && plusPos != std::string_view::npos) {
    if (dashPos > plusPos) {
      return false;
    }

    auto core       = str.substr(0, dashPos);
    auto prerelease = str.substr(dashPos + 1, plusPos - dashPos - 1);
    auto build      = str.substr(plusPos + 1);

    return _semverIsVersionCore(core) && _semverIsPrerelease(prerelease) && _semverIsBuild(build);
  }

  if (dashPos != std::string_view::npos) {
    auto core       = str.substr(0, dashPos);
    auto prerelease = str.substr(dashPos + 1);

    return _semverIsVersionCore(core) && _semverIsPrerelease(prerelease);
  }

  if (plusPos != std::string_view::npos) {
    auto core  = str.substr(0, plusPos);
    auto build = str.substr(plusPos + 1);

    return _semverIsVersionCore(core) && _semverIsBuild(build);
  }

  return false;
}

bool _tryParseU16(std::string_view str, uint16_t& out) {
  if (str.empty()) {
    return false;
  }

  uint32_t u32 = 0;
  for (auto c : str) {
    if (c < '0' || c > '9') {
      return false;
    }

    u32 *= 10;
    u32 += c - '0';

    if (u32 > std::numeric_limits<uint16_t>::max()) {
      return false;
    }
  }

  out = static_cast<uint16_t>(u32);

  return true;
}

bool SemVer::isValid() const {
  if (!this->prerelease.empty() && !_semverIsPrereleaseIdentifier(this->prerelease)) {
    return false;
  }

  if (!this->build.empty() && !_semverIsBuildIdentifier(this->build)) {
    return false;
  }

  return true;
}

std::string SemVer::toString() const {
  std::string str;
  str.reserve(32);

  str += std::to_string(major);
  str += '.';
  str += std::to_string(minor);
  str += '.';
  str += std::to_string(patch);

  if (!prerelease.empty()) {
    str += '-';
    str.append(prerelease.c_str(), prerelease.length());
  }

  if (!build.empty()) {
    str += '+';
    str.append(build.c_str(), build.length());
  }

  return str;
}

bool OpenShock::TryParseSemVer(std::string_view semverStr, SemVer& semver) {
  std::string_view parts[3];
  if (!OpenShock::TryStringSplit(semverStr, '.', parts)) {
    OS_LOGE(TAG, "Failed to split version string: %.*s", semverStr.length(), semverStr.data());
    return false;
  }

  std::string_view majorStr = parts[0], minorStr = parts[1], patchStr = parts[2];

  auto dashIdx = patchStr.find('-');
  if (dashIdx != std::string_view::npos) {
    semver.prerelease = patchStr.substr(dashIdx + 1);
    patchStr          = patchStr.substr(0, dashIdx);
  }

  auto plusIdx = semver.prerelease.find('+');
  if (plusIdx != std::string_view::npos) {
    semver.build      = semver.prerelease.substr(plusIdx + 1);
    semver.prerelease = semver.prerelease.substr(0, plusIdx);
  }

  if (!_tryParseU16(majorStr, semver.major)) {
    OS_LOGE(TAG, "Invalid major version: %.*s", majorStr.length(), majorStr.data());
    return false;
  }

  if (!_tryParseU16(minorStr, semver.minor)) {
    OS_LOGE(TAG, "Invalid minor version: %.*s", minorStr.length(), minorStr.data());
    return false;
  }

  if (!_tryParseU16(patchStr, semver.patch)) {
    OS_LOGE(TAG, "Invalid patch version: %.*s", patchStr.length(), patchStr.data());
    return false;
  }

  if (!semver.prerelease.empty() && !_semverIsPrerelease(semver.prerelease)) {
    OS_LOGE(TAG, "Invalid prerelease: %s", semver.prerelease.c_str());
    return false;
  }

  if (!semver.build.empty() && !_semverIsBuild(semver.build)) {
    OS_LOGE(TAG, "Invalid build: %s", semver.build.c_str());
    return false;
  }

  return true;
}
