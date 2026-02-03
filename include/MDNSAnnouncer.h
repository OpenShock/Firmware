#pragma once

namespace OpenShock::MDNSAnnouncer {
  bool Init();
  bool IsEnabled();
  void SetEnabled(bool enabled);
}  // namespace OpenShock::MDNSAnnouncer
