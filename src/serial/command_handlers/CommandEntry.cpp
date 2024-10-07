#include "serial/command_handlers/CommandEntry.h"

using namespace OpenShock::Serial;

CommandEntry::CommandEntry(std::string_view description, CommandHandler commandHandler)
  : m_description(description)
  , m_commandHandler(commandHandler) 
{
}

CommandArgument& CommandEntry::addArgument(std::string_view name, std::string_view constraint, std::string_view exampleValue, std::string_view constraintExtension) {
  m_arguments.push_back(CommandArgument{name, constraint, exampleValue, constraintExtension});
  return m_arguments.back();
}

CommandGroup::CommandGroup(std::string_view name)
  : m_name(name)
{
}

CommandEntry& CommandGroup::addCommand(std::string_view description, CommandHandler commandHandler) {
  auto cmd = CommandEntry(description, commandHandler);
  m_commands.push_back(cmd);
  return m_commands.back();
}