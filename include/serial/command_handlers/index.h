#pragma once

#include "serial/command_handlers/CommandEntry.h"

#include <vector>

namespace OpenShock::SerialCmds::CommandHandlers {
  OpenShock::SerialCmds::CommandGroup VersionHandler();
  OpenShock::SerialCmds::CommandGroup RestartHandler();
  OpenShock::SerialCmds::CommandGroup SysInfoHandler();
  OpenShock::SerialCmds::CommandGroup EchoHandler();
  OpenShock::SerialCmds::CommandGroup ValidGpiosHandler();
  OpenShock::SerialCmds::CommandGroup RfTxPinHandler();
  OpenShock::SerialCmds::CommandGroup EStopHandler();
  OpenShock::SerialCmds::CommandGroup DomainHandler();
  OpenShock::SerialCmds::CommandGroup AuthTokenHandler();
  OpenShock::SerialCmds::CommandGroup HostnameHandler();
  OpenShock::SerialCmds::CommandGroup NetworksHandler();
  OpenShock::SerialCmds::CommandGroup KeepAliveHandler();
  OpenShock::SerialCmds::CommandGroup JsonConfigHandler();
  OpenShock::SerialCmds::CommandGroup RawConfigHandler();
  OpenShock::SerialCmds::CommandGroup RfTransmitHandler();
  OpenShock::SerialCmds::CommandGroup FactoryResetHandler();

  inline std::vector<OpenShock::SerialCmds::CommandGroup> AllCommandHandlers()
  {
    return {
      VersionHandler(),
      RestartHandler(),
      SysInfoHandler(),
      EchoHandler(),
      ValidGpiosHandler(),
      RfTxPinHandler(),
      EStopHandler(),
      DomainHandler(),
      AuthTokenHandler(),
      HostnameHandler(),
      NetworksHandler(),
      KeepAliveHandler(),
      JsonConfigHandler(),
      RawConfigHandler(),
      RfTransmitHandler(),
      FactoryResetHandler(),
    };
  }
}  // namespace OpenShock::SerialCmds::CommandHandlers
