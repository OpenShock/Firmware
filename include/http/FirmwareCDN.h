#pragma once

#include "http/HTTPRequestManager.h"
#include "ota/FirmwareReleaseInfo.h"
#include "ota/OtaUpdateChannel.h"
#include "SemVer.h"

#include <string>

namespace OpenShock::HTTP::FirmwareCDN {
  struct LatestRelease {
    OpenShock::SemVer version;
    FirmwareReleaseInfo release;
  };

  /// @brief Fetches the latest firmware release for the given channel and board from the repository server.
  /// Makes a single request to /v2/firmware/latest/{channel}?board={board} and parses the JSON response.
  /// @param channel The update channel (stable, beta, develop).
  /// @param repoDomain The repository server domain.
  /// @return The latest firmware version and release info, or an error response.
  HTTP::Response<LatestRelease> GetLatestRelease(OtaUpdateChannel channel, const std::string& repoDomain);

  /// @brief Fetches a specific firmware version's release info from the repository server.
  /// Makes a request to /v2/firmware/versions/{version}?board={board} and parses the JSON response.
  /// @param version The specific firmware version to fetch.
  /// @param repoDomain The repository server domain.
  /// @return The firmware release info, or an error response.
  HTTP::Response<FirmwareReleaseInfo> GetRelease(const OpenShock::SemVer& version, const std::string& repoDomain);
}  // namespace OpenShock::HTTP::FirmwareCDN
