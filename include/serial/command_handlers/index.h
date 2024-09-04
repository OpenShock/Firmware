#pragma once

#include "serial/command_handlers/impl/SerialCmdHandler.h"

namespace OpenShock::Serial::CommandHandlers {
  std::vector<OpenShock::Serial::CommandHandlerEntry> AuthTokenHandler();
  std::vector<OpenShock::Serial::CommandHandlerEntry> DomainHandler();
  std::vector<OpenShock::Serial::CommandHandlerEntry> EchoHandler();
  std::vector<OpenShock::Serial::CommandHandlerEntry> FactoryResetHandler();
  std::vector<OpenShock::Serial::CommandHandlerEntry> JsonConfigHandler();
  std::vector<OpenShock::Serial::CommandHandlerEntry> KeepAliveHandler();
  std::vector<OpenShock::Serial::CommandHandlerEntry> LcgOverrideHandler();
  std::vector<OpenShock::Serial::CommandHandlerEntry> NetworksHandler();
  std::vector<OpenShock::Serial::CommandHandlerEntry> RawConfigHandler();
  std::vector<OpenShock::Serial::CommandHandlerEntry> RestartHandler();
  std::vector<OpenShock::Serial::CommandHandlerEntry> RfTransmitHandler();
  std::vector<OpenShock::Serial::CommandHandlerEntry> RfTxPinHandler();
  std::vector<OpenShock::Serial::CommandHandlerEntry> SysInfoHandler();
  std::vector<OpenShock::Serial::CommandHandlerEntry> ValidGpiosHandler();
  std::vector<OpenShock::Serial::CommandHandlerEntry> VersionHandler();
}  // namespace OpenShock::Serial::CommandHandlers
