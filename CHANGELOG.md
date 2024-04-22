# Version 1.2.0-rc.1 Release Notes

This is the first release candidate for version 1.2.0.

## Highlight

- Added support for **998DR** Petrainer RF protocol.

## Major Updates

- Add command to get/set api domain.
- Add command to get/set/clear override for Live Control Gateway (LCG) domain.

## Minor Updates

- Change transmission end command to last for 300 ms.
- Increase WDT timeout during OTA updates to prevent watchdog resets.
- Remove non thread-safe RF sequence caching.
- Update flatbuffers to 23.5.26.
- Start utilizing StringView more to reduce memory and CPU usage.
- Small code cleanup and refactoring.

# Version 1.1.2 Release Notes

- Add support for OpenShock Core V2 Hardware

# Version 1.1.1 Release Notes

This release is increases the stability and performance of the firmware, as well as fixes some minor bugs.

## Highlight

- Enabled release builds for improved firmware size, performance, and stability.

## Minor Updates

- Improved RFTransmitter delay logic to wait patiently for commands if it has no transmissions to send.
- Increased RFTransmitter performance margins to avoid command stacking and delays.
- Updated build script to properly identify git-tag triggered github action runs.

## Bug Fixes

- Fixed a bug where the RFTransmitter loop would never delay, causing other tasks running on the same core to completely halt.
- Removed null check on credentials password received in frontend, as null is expected due to sensitive data removal.

# Version 1.1.1-rc.6 Release Notes

Inlined the wait time check in RFTransmitter to re-check if we added any commands on receiving a event.

# Version 1.1.1-rc.5 Release Notes

Fixed a bug where the RFTransmitter loop would never delay, causing other tasks running on the same core to completely halt.

# Version 1.1.1-rc.4 Release Notes

Fix tag check again, this time for real.

# Version 1.1.1-rc.3 Release Notes

Increased performance margins for RFTransmitter to prevent commands from stacking up and getting delayed.

Fixed python build script git-tag check to check `GIT_REF_NAME` instead of incorrect `GIT_BASE_REF` which caused it to build in debug mode.

# Version 1.1.1-rc.2 Release Notes

Removed null check on credentials password received in frontend, as null is expected due to sensitive data removal.

# Version 1.1.1-rc.1 Release Notes

In this release we enabled release builds, resulting in smaller, faster, and more stable firmware.

# Version 1.1.0 Release Notes

It's finally here! The 1.1.0 release of OpenShock is now available for download.

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
- Fixed updateID not being sent with BootStatus message.
- Make OTA update status reporting smoother.
- Fix what firmware boot type firmware reports to server.
- Reduced latency, allocations, and network traffic for reporting wifi network scan results, making the networks instantly available in the frontend and improving the reliability of the captive portal.

# Version 1.1.0-rc.6 Release Notes

Bugfixes:

- Reduced latency, allocations, and network traffic for reporting wifi network scan results, making the networks instantly available in the frontend and improving the reliability of the captive portal.

# Version 1.1.0-rc.5 Release Notes

Bugfixes:

- Fix what firmware boot type firmware reports to server.

# Version 1.1.0-rc.4 Release Notes

Bugfixes:

- Make OTA update status reporting smoother.

# Version 1.1.0-rc.3 Release Notes

Bugfixes:

- Fixed updateID not being sent with BootStatus message.

# Version 1.1.0-rc.2 Release Notes

This is the RC (Release Candidate) 2 for version 1.1.0

We did a couple of bugfixes:

- Fixed User-Agent header not being set on websocket connections.
- Stopped frontend from requesting to connect to a secured network without a password.
- Do sanity checking on pairing code length in firmware to return a proper error message early.
- Fixed some SemVer parsing logic.

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
