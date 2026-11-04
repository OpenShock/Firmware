#include <freertos/FreeRTOS.h>

#include "serial_console/command_handlers/common.h"

#include "Convert.h"
#include "enums/StatusLed.h"
#include "StringHelpers.h"
#include "visual/VisualStateManager.h"

using OpenShock::LedSignal;
using OpenShock::LedType;

static void handleLedModeCommand(std::string_view arg, bool isAutomated)
{
  (void)isAutomated;

  arg = OpenShock::StringTrim(arg);
  if (arg.empty()) {
    SERPR_RESPONSE("LedMode|%s", OpenShock::LedModeName(OpenShock::VisualStateManager::GetLedMode()));
    return;
  }

  auto mode = OpenShock::TryParseLedMode(arg);
  if (!mode) {
    SERPR_ERROR("Invalid argument (must be all, critical or off)");
    return;
  }

  OpenShock::SerialCmds::ReportSaved(OpenShock::VisualStateManager::SetLedMode(*mode));
}

static void handleLedBrightnessCommand(std::string_view arg, bool isAutomated)
{
  (void)isAutomated;

  arg = OpenShock::StringTrim(arg);
  if (arg.empty()) {
    using OpenShock::VisualStateManager::GetLedBrightness;
    SERPR_RESPONSE("LedBrightness|mono|critical|%u", GetLedBrightness(LedType::Mono, LedSignal::Critical));
    SERPR_RESPONSE("LedBrightness|mono|normal|%u", GetLedBrightness(LedType::Mono, LedSignal::Normal));
    SERPR_RESPONSE("LedBrightness|rgb|critical|%u", GetLedBrightness(LedType::Rgb, LedSignal::Critical));
    SERPR_RESPONSE("LedBrightness|rgb|normal|%u", GetLedBrightness(LedType::Rgb, LedSignal::Normal));
    return;
  }

  auto parts = OpenShock::TryStringSplit<3>(arg, ' ');
  if (!parts) {
    SERPR_ERROR("Invalid arguments (expected <mono|rgb> <critical|normal> <percent>)");
    return;
  }

  auto type    = OpenShock::TryParseLedType((*parts)[0]);
  auto signal  = OpenShock::TryParseLedSignal((*parts)[1]);
  auto percent = OpenShock::Convert::ToUint8(OpenShock::StringTrim((*parts)[2]));
  if (!type) {
    SERPR_ERROR("Invalid LED type (must be mono or rgb)");
    return;
  }
  if (!signal) {
    SERPR_ERROR("Invalid signal (must be critical or normal)");
    return;
  }
  if (!percent || *percent > 100) {
    SERPR_ERROR("Invalid brightness (must be a percent, 0-100)");
    return;
  }

  OpenShock::SerialCmds::ReportSaved(OpenShock::VisualStateManager::SetLedBrightness(*type, *signal, *percent));
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::LedHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("led");

  group.addCommand("mode", "Get which states the status LEDs show", handleLedModeCommand);
  auto& setModeCommand = group.addCommand("mode", "Set which states the status LEDs show: all, critical (only an E-Stop or a critical error) or off", handleLedModeCommand);
  setModeCommand.addArgument("mode", "all, critical or off", "critical");

  group.addCommand("brightness", "Get the status LED brightness, per LED type and signal", handleLedBrightnessCommand);
  auto& setBrightnessCommand = group.addCommand("brightness", "Set the brightness of one LED type for critical (E-Stop, critical error) or normal patterns", handleLedBrightnessCommand);
  setBrightnessCommand.addArgument("type", "mono or rgb", "rgb");
  setBrightnessCommand.addArgument("signal", "critical or normal", "normal");
  setBrightnessCommand.addArgument("percent", "0-100, of the LED's full output; RGB above 30% logs a warning", "8");

  return group;
}
