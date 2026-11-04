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

  if (!OpenShock::VisualStateManager::HasAnyLed()) {
    SERPR_ERROR("This board has no status LED");
    return;
  }

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

  using OpenShock::VisualStateManager::GetLedBrightness;
  using OpenShock::VisualStateManager::HasLed;

  arg = OpenShock::StringTrim(arg);
  if (arg.empty()) {
    // Only the LEDs the board has
    if (!OpenShock::VisualStateManager::HasAnyLed()) {
      SERPR_ERROR("This board has no status LED");
      return;
    }
    for (LedType type : {LedType::Mono, LedType::Rgb}) {
      if (!HasLed(type)) {
        continue;
      }
      const char* name = type == LedType::Mono ? "mono" : "rgb";
      SERPR_RESPONSE("LedBrightness|%s|critical|%u", name, GetLedBrightness(type, LedSignal::Critical));
      SERPR_RESPONSE("LedBrightness|%s|normal|%u", name, GetLedBrightness(type, LedSignal::Normal));
    }
    return;
  }

  auto parts = OpenShock::TryStringSplit<3>(arg, ' ');
  if (!parts) {
    SERPR_ERROR("Invalid arguments (expected <mono|rgb> <critical|normal> <0-65535>)");
    return;
  }

  auto type    = OpenShock::TryParseLedType((*parts)[0]);
  auto signal  = OpenShock::TryParseLedSignal((*parts)[1]);
  auto value   = OpenShock::Convert::ToUint16(OpenShock::StringTrim((*parts)[2]));
  if (!type) {
    SERPR_ERROR("Invalid LED type (must be mono or rgb)");
    return;
  }
  if (!HasLed(*type)) {
    SERPR_ERROR("This board has no %s LED", *type == LedType::Mono ? "mono" : "rgb");
    return;
  }
  if (!signal) {
    SERPR_ERROR("Invalid signal (must be critical or normal)");
    return;
  }
  if (!value) {
    SERPR_ERROR("Invalid brightness (must be 0-65535)");
    return;
  }

  OpenShock::SerialCmds::ReportSaved(OpenShock::VisualStateManager::SetLedBrightness(*type, *signal, *value));
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
  setBrightnessCommand.addArgument("brightness", "0-65535, of the LED's full output; RGB above 19661 (30%) logs a warning", "5140");

  return group;
}
