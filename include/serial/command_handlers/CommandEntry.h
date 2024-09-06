#pragma once

#include <string_view>
#include <vector>

namespace OpenShock::Serial {
  class CommandArgument {
  public:
    std::string_view name;
    std::string_view constraint;
    std::string_view constraintExtension;
    std::string_view exampleValue;
  };

  class CommandEntry {
  public:
    typedef void (*CommandHandler)(std::string_view);

    CommandEntry(std::string_view description, void (*commandHandler)(std::string_view));

    inline std::string_view description() const { return m_description; }
    inline const std::vector<CommandArgument>& arguments() const { return m_arguments; }
    inline const CommandHandler commandHandler() const { return m_commandHandler; }

    CommandArgument& addArgument(std::string_view name, std::string_view constraint, std::string_view exampleValue);

  private:
    std::string_view m_description;
    std::vector<CommandArgument> m_arguments;
    CommandHandler m_commandHandler;
  };

  class CommandGroup {
  public:
    CommandGroup(std::string_view name);
    inline CommandGroup(const CommandGroup& other) {
      m_name = other.m_name;
      m_commands = other.m_commands;
    }

    inline std::string_view name() const { return m_name; }
    inline const std::vector<CommandEntry>& commands() const { return m_commands; }

    CommandEntry& addCommand(std::string_view description, void (*commandHandler)(std::string_view));
  private:
    std::string_view m_name;
    std::vector<CommandEntry> m_commands;
  };
}  // namespace OpenShock::Serial::CommandHandlers
