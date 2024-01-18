# Version 1.1.0-rc.1 Release Notes

It's been a while, and we think it's time for another beta release :smile:

This is the RC (Release Candidate) 1 for version 1.1.0, hence the naming: 1.1.0-rc.1

This update is packed with some major enhancements and numerous improvements that we believe will improve your experience using your device.

From introducing seamless Over-The-Air updates, adding support for new hardware, to enhancing the overall functionality and stability of the system, we've worked hard to improve upon our last release.

We've also squashed some pesky bugs and made various minor updates to streamline and optimize your experience.

Here’s what’s new:

## Major Updates

- **OTA (Over-The-Air) Updates**:
  - Introduced a seamless OTA update capability, device can now be updated with the click of a button.
  - Features:
    - Updates can be triggered remotely via the OpenShock website.
    - Device can automatically check for updates at a configured interval.
    - Update state will be streamed back to frontend so you can see the status of your device in real time.
  - Provided an option to deactivate each of these features individually through the Captive Portal for users preferring manual control.
- **Support for OpenShock Core V1**: Added support for @nullstalgia custom PCB, which is [fully open-source](https://github.com/nullstalgia/OpenShock-Hardware/tree/main/Core).
- **Captive Behavior Enhancement**: Phones and PC's now prompt the user with the Captive Portal upon WiFi connection.
- **More serial commands**:
  - Read/Write configuration in JSON or raw binary format.
  - Shocker command execution via serial.
  - GPIO pin listing, excluding reserved pins.
  - Serial command echo support for terminals lacking this feature.
- **Reworked partitions:** Moved configuration into its own partition, ensuring it persists across updates
- **Reserved pins**:
  - Added support for reserved pins so users can no longer use reserved pins that might lead to ESP instability.
  - User will now receive an error upon trying to set anything to use these pins.
  - The available pins can be listed via the `AvailGPIO` serial command.
- **Shocker keepalive**:
  - Improved shocker responsiveness by sending keepalive messages to them at a interval.
  - This will prevent the shockers from entering sleep mode.
  - Has option to be disabled via the `keepalive` serial command.
- **Config Handler Overhaul**:
  - Rewrote config handler to be more modular and make it easier to expand upon the code base.
  - Each config section is now seperated into its own file and class.

## Minor Updates

- Firmware upload now includes an MD5 sum.
- Enhanced reliability of WiFi scanning.
- Status LEDs:
  - Reworked some code for LED pattern and state management.
  - Added support for WS2812B RGB (Gamer :sunglasses:) LEDs.
- Improved WiFi connectivity speed post-setup.
- Updated Captive Portal color palette.
- Dependency cleanup:
  - Removed ArduinoJSON
  - Removed nonstd/span
- Removed Arduino-style loop behaviors, replaced with freeRTOS tasks.
- Improved FreeRTOS task management.
- Implemented self-ratelimiting on httpclient.
- Enhanced error checking in captive portal and firmware.
- CodeQL code quality checks integrated into CI/CD pipeline.
- Utilized filesystem partition hash as ETag for content caching.
- Improved logs to be more consise and verbose.
- Miscellaneous code cleanup, refactoring, and optimizations.

## Bug Fixes

- Resolved issue with WiFi scans getting stuck.
- Fixed connection problems with unsecured networks.
- Altered CommandHandler to use a queue kill message, preventing panic when deleting a mid-listening queue.
- Fixed ESP becoming unresponsive when the looptask would get deleted by captive portal deconstructor due to a missing null check.

# Version 1.0.0

- We now support **six different boards**:
  - Pishock (2023)
  - Pishock Lite (2021)
  - Seeed Xiao ESP32S3
  - Wemos D1 Mini ESP32
  - Wemos Lolin S2 Mini
  - Wemos Lolin S3
- All communication is now **websocket based** and using flatbuffers, allowing for even lower latency between the server and the ESP, with lower resource consumption.
- The **Captive Portal** got a MASSIVE overhaul;
- Serial commands have gotten alot better.
- Improved board stability and configurability.
- Added support for having a E-Stop (emergency stop) connected to ESP as a panic button. Thanks to @nullstalgia ❤️
- And _much, much_ more behind the scenes, including bugfixes and code cleanup.

# Version 1.0.0-rc.4

# Version 1.0.0-rc.3

# Version 1.0.0-rc.2

# Version 1.0.0-rc.1

# Version v0.8.1
