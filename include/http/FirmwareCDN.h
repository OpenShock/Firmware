#pragma once

#include "http/HTTPRequestManager.h"
#include "ota/FirmwareBinaryHash.h"
#include "ota/FirmwareReleaseInfo.h"
#include "ota/OtaUpdateChannel.h"
#include "SemVer.h"

#include <string_view>

namespace OpenShock::HTTP::FirmwareCDN {
  /// @brief Fetches the firmware version for the given channel from the firmware CDN.
  /// Valid response codes: 200, 304
  /// @param channel The channel to fetch the firmware version for.
  /// @return The firmware version or an error response.
  HTTP::Response<OpenShock::SemVer> GetFirmwareVersion(OtaUpdateChannel channel);

  /// @brief Fetches the list of available boards for the given firmware version from the firmware CDN.
  /// Valid response codes: 200, 304
  /// @param version The firmware version to fetch the boards for.
  /// @return The list of available boards or an error response.
  HTTP::Response<std::vector<std::string>> GetFirmwareBoards(const OpenShock::SemVer& version);

  /// @brief Fetches the binary hashes for the given firmware version from the firmware CDN.
  /// Valid response codes: 200, 304
  /// @param version The firmware version to fetch the binary hashes for.
  /// @return The binary hashes or an error response.
  HTTP::Response<std::vector<FirmwareBinaryHash>> GetFirmwareBinaryHashes(const OpenShock::SemVer& version);

  /// @brief Fetches the firmware release information for the given firmware version from the firmware CDN.
  /// Valid response codes: 200, 304
  /// @param version The firmware version to fetch the release information for.
  /// @return The firmware release information or an error response.
  HTTP::Response<FirmwareReleaseInfo> GetFirmwareReleaseInfo(const OpenShock::SemVer& version);
}  // namespace OpenShock::HTTP::FirmwareCDN
