#pragma once

#include "serial/command_handlers/CommandEntry.h"

#include <vector>

namespace OpenShock::Serial::CommandHandlers {
  OpenShock::Serial::CommandGroup AuthTokenHandler();
  OpenShock::Serial::CommandGroup DomainHandler();
  OpenShock::Serial::CommandGroup EchoHandler();
  OpenShock::Serial::CommandGroup FactoryResetHandler();
  OpenShock::Serial::CommandGroup JsonConfigHandler();
  OpenShock::Serial::CommandGroup KeepAliveHandler();
  OpenShock::Serial::CommandGroup LcgOverrideHandler();
  OpenShock::Serial::CommandGroup HostnameHandler();
  OpenShock::Serial::CommandGroup NetworksHandler();
  OpenShock::Serial::CommandGroup RawConfigHandler();
  OpenShock::Serial::CommandGroup RestartHandler();
  OpenShock::Serial::CommandGroup RfTransmitHandler();
  OpenShock::Serial::CommandGroup RfTxPinHandler();
  OpenShock::Serial::CommandGroup SysInfoHandler();
  OpenShock::Serial::CommandGroup ValidGpiosHandler();
  OpenShock::Serial::CommandGroup VersionHandler();
  inline std::vector<OpenShock::Serial::CommandGroup> AllCommandHandlers() {
    return {
      VersionHandler(),
      RestartHandler(),
      SysInfoHandler(),
      EchoHandler(),
      ValidGpiosHandler(),
      RfTxPinHandler(),
      DomainHandler(),
      AuthTokenHandler(),
      LcgOverrideHandler(),
      HostnameHandler(),
      NetworksHandler(),
      KeepAliveHandler(),
      JsonConfigHandler(),
      RawConfigHandler(),
      RfTransmitHandler(),
      FactoryResetHandler(),
    };
  }
}  // namespace OpenShock::Serial::CommandHandlers
