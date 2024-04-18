#include "SemVer.h"

const char* const TAG = "SemVer";

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
constexpr bool _semverIsDigits(StringView str) {
  if (str.isNullOrEmpty()) {
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
constexpr bool _semverIsIdentifierChararacters(StringView str) {
  if (str.isNullOrEmpty()) {
    return false;
  }

  for (auto c : str) {
    if (!_semverIsIdentifierChararacter(c)) {
      return false;
    }
  }

  return true;
}
constexpr bool _semverIsNumericIdentifier(StringView str) {
  if (str.isNullOrEmpty()) {
    return false;
  }

  if (str.length() == 1) {
    return _semverIsDigit(str[0]);
  }

  return _semverIsPositiveDigit(str[0]) && _semverIsDigits(str.substr(1));
}
constexpr bool _semverIsAlphanumericIdentifier(StringView str) {
  if (str.isNullOrEmpty()) {
    return false;
  }

  if (str.length() == 1) {
    return _semverIsNonDigit(str[0]);
  }

  std::size_t nonDigitPos = StringView::npos;
  for (std::size_t i = 0; i < str.length(); ++i) {
    if (_semverIsNonDigit(str[i])) {
      nonDigitPos = i;
      break;
    }
  }

  if (nonDigitPos == StringView::npos) {
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
constexpr bool _semverIsBuildIdentifier(StringView str) {
  return _semverIsAlphanumericIdentifier(str) || _semverIsDigits(str);
}
constexpr bool _semverIsPrereleaseIdentifier(StringView str) {
  return _semverIsAlphanumericIdentifier(str) || _semverIsNumericIdentifier(str);
}
constexpr bool _semverIsDotSeperatedBuildIdentifiers(StringView str) {
  if (str.isNullOrEmpty()) {
    return false;
  }

  auto dotIdx = str.find('.');
  while (dotIdx != StringView::npos) {
    auto part = str.substr(0, dotIdx);
    if (!_semverIsBuildIdentifier(part)) {
      return false;
    }

    str = str.substr(dotIdx + 1);
    dotIdx = str.find('.');
  }

  return _semverIsBuildIdentifier(str);
}
constexpr bool _semverIsDotSeperatedPreleaseIdentifiers(StringView str) {
  if (str.isNullOrEmpty()) {
    return false;
  }

  auto dotIdx = str.find('.');
  while (dotIdx != StringView::npos) {
    auto part = str.substr(0, dotIdx);
    if (!_semverIsPrereleaseIdentifier(part)) {
      return false;
    }

    str = str.substr(dotIdx + 1);
    dotIdx = str.find('.');
  }

  return _semverIsPrereleaseIdentifier(str);
}
const auto _semverIsPatch = _semverIsNumericIdentifier;
const auto _semverIsMinor = _semverIsNumericIdentifier;
const auto _semverIsMajor = _semverIsNumericIdentifier;
const auto _semverIsPrerelease = _semverIsDotSeperatedPreleaseIdentifiers;
const auto _semverIsBuild = _semverIsDotSeperatedBuildIdentifiers;
bool _semverIsVersionCore(StringView str) {
  if (str.isNullOrEmpty()) {
    return false;
  }

  auto parts = str.split('.');
  if (parts.size() != 3) {
    return false;
  }

  return _semverIsMajor(parts[0]) && _semverIsMinor(parts[1]) && _semverIsPatch(parts[2]);
}
bool _semverIsSemver(StringView str) {
  if (str.isNullOrEmpty()) {
    return false;
  }

  auto dashPos = str.find('-');
  auto plusPos = str.find('+');

  if (dashPos == StringView::npos && plusPos == StringView::npos) {
    return _semverIsVersionCore(str);
  }

  if (dashPos != StringView::npos && plusPos != StringView::npos) {
    if (dashPos > plusPos) {
      return false;
    }

    auto core = str.substr(0, dashPos);
    auto prerelease = str.substr(dashPos + 1, plusPos - dashPos - 1);
    auto build = str.substr(plusPos + 1);

    return _semverIsVersionCore(core) && _semverIsPrerelease(prerelease) && _semverIsBuild(build);
  }

  if (dashPos != StringView::npos) {
    auto core = str.substr(0, dashPos);
    auto prerelease = str.substr(dashPos + 1);

    return _semverIsVersionCore(core) && _semverIsPrerelease(prerelease);
  }

  if (plusPos != StringView::npos) {
    auto core = str.substr(0, plusPos);
    auto build = str.substr(plusPos + 1);

    return _semverIsVersionCore(core) && _semverIsBuild(build);
  }

  return false;
}

bool _tryParseU16(OpenShock::StringView str, std::uint16_t& out) {
  if (str.isNullOrEmpty()) {
    return false;
  }

  std::uint32_t u32 = 0;
  for (auto c : str) {
    if (c < '0' || c > '9') {
      return false;
    }

    u32 *= 10;
    u32 += c - '0';

    if (u32 > std::numeric_limits<std::uint16_t>::max()) {
      return false;
    }
  }

  out = static_cast<std::uint16_t>(u32);

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

bool OpenShock::TryParseSemVer(StringView semverStr, SemVer& semver) {
  auto parts = semverStr.split('.');
  if (parts.size() < 3) {
    ESP_LOGE(TAG, "Must have at least 3 parts: %.*s", semverStr.length(), semverStr.data());
    return false;
  }

  StringView majorStr = parts[0], minorStr = parts[1], patchStr = parts[2];

  auto dashIdx = patchStr.find('-');
  if (dashIdx != StringView::npos) {
    semver.prerelease = patchStr.substr(dashIdx + 1);
    patchStr = patchStr.substr(0, dashIdx);
  }

  auto plusIdx = semver.prerelease.find('+');
  if (plusIdx != StringView::npos) {
    semver.build = semver.prerelease.substr(plusIdx + 1);
    semver.prerelease = semver.prerelease.substr(0, plusIdx);
  }

  if (!_tryParseU16(majorStr, semver.major)) {
    ESP_LOGE(TAG, "Invalid major version: %.*s", majorStr.length(), majorStr.data());
    return false;
  }

  if (!_tryParseU16(minorStr, semver.minor)) {
    ESP_LOGE(TAG, "Invalid minor version: %.*s", minorStr.length(), minorStr.data());
    return false;
  }

  if (!_tryParseU16(patchStr, semver.patch)) {
    ESP_LOGE(TAG, "Invalid patch version: %.*s", patchStr.length(), patchStr.data());
    return false;
  }

  if (!semver.prerelease.empty() && !_semverIsPrerelease(semver.prerelease)) {
    ESP_LOGE(TAG, "Invalid prerelease: %s", semver.prerelease.c_str());
    return false;
  }

  if (!semver.build.empty() && !_semverIsBuild(semver.build)) {
    ESP_LOGE(TAG, "Invalid build: %s", semver.build.c_str());
    return false;
  }

  return true;
}
