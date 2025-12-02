#pragma once

#include "CommandArgument.h"
#include "CommandHandler.h"

#include <string_view>
#include <vector>

namespace OpenShock::Serial {
  class CommandEntry {
  public:
    CommandEntry(std::string_view description, CommandHandler commandHandler);
    CommandEntry(std::string_view name, std::string_view description, CommandHandler commandHandler);

    inline std::string_view name() const { return m_name; }
    inline std::string_view description() const { return m_description; }
    inline const std::vector<CommandArgument>& arguments() const { return m_arguments; }
    inline const CommandHandler commandHandler() const { return m_commandHandler; }

    CommandArgument& addArgument(std::string_view name, std::string_view constraint, std::string_view exampleValue, std::vector<std::string_view> constraintExtensions = {});

  private:
    std::string_view m_name;
    std::string_view m_description;
    std::vector<CommandArgument> m_arguments;
    CommandHandler m_commandHandler;
  };
}  // namespace OpenShock::Serial
