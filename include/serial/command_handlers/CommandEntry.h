#pragma once

#include <string_view>
#include <vector>

namespace OpenShock::Serial {
  typedef void (*CommandHandler)(std::string_view arg, bool isAutomated);

  class CommandArgument {
  public:
    std::string_view name;
    std::string_view constraint;
    std::string_view exampleValue;
    std::vector<std::string_view> constraintExtensions;
  };

  class CommandEntry {
  public:
    CommandEntry(std::string_view description, CommandHandler commandHandler);
    CommandEntry(std::string_view name, std::string_view description, CommandHandler commandHandler);

    inline std::string_view name() const { return m_name; }
    inline std::string_view description() const { return m_description; }
    inline const std::vector<CommandArgument>& arguments() const { return m_arguments; }
    inline CommandHandler commandHandler() { return m_commandHandler; }

    CommandArgument& addArgument(std::string_view name, std::string_view constraint, std::string_view exampleValue, std::vector<std::string_view> constraintExtensions = {});

  private:
    std::string_view m_name;
    std::string_view m_description;
    std::vector<CommandArgument> m_arguments;
    CommandHandler m_commandHandler;
  };

  class CommandGroup {
  public:
    CommandGroup() = default;
    CommandGroup(std::string_view name);
    CommandGroup(CommandGroup&& other)                 = default;
    CommandGroup(const CommandGroup& other)            = default;
    CommandGroup& operator=(CommandGroup&& other)      = default;
    CommandGroup& operator=(const CommandGroup& other) = default;

    inline std::string_view name() const { return m_name; }
    inline std::vector<CommandEntry>& commands() { return m_commands; }
    inline const std::vector<CommandEntry>& commands() const { return m_commands; }

    CommandEntry& addCommand(std::string_view description, CommandHandler commandHandler);
    CommandEntry& addCommand(std::string_view name, std::string_view description, CommandHandler commandHandler);

  private:
    std::string_view m_name;
    std::vector<CommandEntry> m_commands;
  };
}  // namespace OpenShock::Serial
