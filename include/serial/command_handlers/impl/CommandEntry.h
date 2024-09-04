#pragma once

#include "StringView.h"

#include <vector>

namespace OpenShock::Serial::CommandHandlers {
  class CommandArgument {
  public:
    StringView name;
    StringView constraint;
    StringView constraintExtension;
    StringView exampleValue;
  };
  class CommandEntry {
  public:
    CommandEntry(StringView name, StringView description, void (*commandHandler)(StringView));

    inline StringView name() const { return m_name; }
    inline StringView description() const { return m_description; }

    CommandArgument& addArgument(StringView name, StringView constraint, StringView exampleValue);

  private:
    StringView m_name;
    StringView m_description;
    std::vector<CommandArgument> m_arguments;
    void (*m_commandHandler)(StringView);
  };
}  // namespace OpenShock::Serial::CommandHandlers
