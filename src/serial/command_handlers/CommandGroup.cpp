#include "serial/command_handlers/CommandGroup.h"

using namespace OpenShock::Serial;

CommandGroup::CommandGroup(std::string_view name)
  : m_name(name)
{
}

CommandEntry& CommandGroup::addCommand(std::string_view description, CommandHandler commandHandler)
{
  m_commands.emplace_back(description, commandHandler);
  return m_commands.back();
}

CommandEntry& CommandGroup::addCommand(std::string_view name, std::string_view description, CommandHandler commandHandler)
{
  m_commands.emplace_back(name, description, commandHandler);
  return m_commands.back();
}
