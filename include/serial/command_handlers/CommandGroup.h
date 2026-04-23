#pragma once

#include "CommandEntry.h"
#include "CommandHandler.h"

#include <string_view>
#include <vector>

namespace OpenShock::Serial {
  class CommandGroup {
  public:
    CommandGroup() = default;
    CommandGroup(std::string_view name);
    CommandGroup(CommandGroup&& other)                 = default;
    CommandGroup(const CommandGroup& other)            = default;
    CommandGroup& operator=(CommandGroup&& other)      = default;
    CommandGroup& operator=(const CommandGroup& other) = default;

    constexpr std::string_view name() const { return m_name; }
    const std::vector<CommandEntry>& commands() const { return m_commands; }

    CommandEntry& addCommand(std::string_view description, CommandHandler commandHandler);
    CommandEntry& addCommand(std::string_view name, std::string_view description, CommandHandler commandHandler);

  private:
    std::string_view m_name;
    std::vector<CommandEntry> m_commands;
  };
}  // namespace OpenShock::Serial
