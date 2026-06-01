# Version 1.5.2 Release Notes

Feature release restoring missing WellturnT330 support that was intended for 1.5.0.

### Bug Fixes

- Added **WellturnT330 Support** - Added full support for **WellturnT330** devices. This was originally planned for 1.5.0 but was unintentionally left out.

**Full Changelog: [1.5.1 -> 1.5.2](https://github.com/OpenShock/Firmware/compare/1.5.1...1.5.2)**




# Version 1.5.1 Release Notes

Hotfix release addressing a safety-critical E-Stop bypass.

### Bug Fixes

- **E-Stop** - Fixed shocker commands being transmitted while E-Stop was active. Commands are now rejected at the entry point (`CommandHandler::HandleCommand`) and discarded in the RF transmit task's receive loop when E-Stop is engaged.

**Full Changelog: [1.5.0 -> 1.5.1](https://github.com/OpenShock/Firmware/compare/1.5.0...1.5.1)**




# Version 1.5.0 Release Notes

This release is a major firmware update bringing a fully reworked RF transmitter pipeline for more reliable shocker communication, a new T330 shocker protocol, an RFC 8908-compliant captive portal, a rebuilt frontend (Svelte 5 + shadcn), and significant improvements to E-Stop handling and rate limiting.

***Warning: Petrainer users will need to re-pair their shockers once they update their hub due to protocol changes.***

### Highlights

- Added **T330 shocker protocol** support
- Completely reworked RF behaviour for more reliable shocker control
- ***WAY*** more and responsive stable captive portal (It's a night and day difference)
- Rebuilt Captive Portal UI to match frontend style.
- **E-Stop button** is now significantly more reliable with a re-arm grace period
- **Wi-Fi RSSI telemetry** reported by the hub for connection health display in frontend

### Major Improvements

- **RMT / Radio Transmitter**
  - Added **T330** shocker protocol support
  - Fixed timing issues for **CaiXianlin** and improved **PET998DR** handling
  - Lots of minor optimizations, refactors and guardrails to improve radio reliability

- **E-Stop**
  - More reliable E-Stop state transitions with change detection, eliminating event spamming
  - Re-arm grace period after clearing prevents re-triggering from switch bounce or noise

- **HTTP**
  - Rate limiter now tracks requests more efficiently with corrected cleanup timing and more predictable blocking

- **Captive Portal**
  - Captive portal is now refactored to operate more like **RFC 8908 spec** with improved responsiveness
  - Migrated from Svelte 4 + Skeleton UI to **Svelte 5 + shadcn-svelte** with **Tailwind CSS v4**
  - UI palette synced with OpenShock website; reduced bundle size
  - Added unit testing and Svelte check steps; migrated from npm to **pnpm**
  - Removed mDNS server from captive portal
  - Removed the "AbsolutelySureButton" UI element, meant as a joke but it got old and obnoxious

- **AssignLCG & Gateway**
  - AssignLCG uses the new backend endpoint and reports firmware version; legacy LCG override removed
  - Improved HTTP error/status consistency and 401 token recovery

### Minor Updates / Optimizations

- **Wi-Fi RSSI telemetry** now reported by the hub so the OpenShock.app frontend can display connection health
- Replaced all `ESP.restart()` calls with ESP-IDF native `esp_restart()`
- Improved OTA update flow and crash-loop resilience
- Safer integer formatting, reduced unnecessary string copying, and cleaner command/serial logic
- **CA certificate bundle** updated to December 2025 Mozilla extract, this bundle is not used yet but will be once we drop Arduino for ESP-IDF
- **CDN** migrated from Cloudflare R2 (rclone) to Bunny CDN (SFTP) with cache purge support
- **CI/CD** updated: Actions bumped (checkout v6, codeql v4, download-artifact v7), timeout limits on all jobs, pnpm adopted, Node.js updated
- Expanded and cleaned up compiler warnings; PlatformIO, espressif32, FlatBuffers, and other dependency updates

### Bug Fixes

- Addressed variable initialization and type casting correctness issues
- Fixed timing issues in **CaiXianlin** and **PET998DR** shocker protocols

**Full Changelog: [1.4.0 -> 1.5.0](https://github.com/OpenShock/Firmware/compare/1.4.0...1.5.0)**




# Version 1.4.0 Release Notes

This release is packed with bugfixes, optimizations, code cleanup, prepwork for ESP-IDF, and some features!

### Highlights

- Add support for configuring hostname of ESP via Serial.
- Add support for configuring Emergency Stop via Captive Portal and Serial.
- Report available GPIO pins to Captive Portal Frontend.
- Massively refactor serial command handler.

### Optimizations

- Bump platform-espressif32 to version 6.9.
- Start using C++17 features including std::string_view.
- Clean up platformio.ini file.
- Lots of miscellanious code cleanup.
- Implement custom zero-copy type conversion methods with better error checking.
- Reduce log spam by the arduino library.
- Improve error handling of gpio pin selection.
- Attempt to make more sense out of the 998DR protocol serializer.

**Full Changelog: [1.3.0 -> 1.4.0](https://github.com/OpenShock/Firmware/compare/1.3.0...1.4.0)**




# Version 1.3.0 Release Notes

This release adds support for more boards, has more bugfixes, better error handling, and optimization/cleanup.

### Highlight

- Added support for **DFRobot Firebeetle**, **Wemos S3 Mini** and **WaveShare S3 Zero** boards.

### Minor Updates

- Re-Add **PET998DR** Quiet Postamble.
- Fix CaiXianlin protocol sending non-zero when doing a beep command.
- Moved schema files to seperate repository.
- Improve error handling and logging.
- Dependency updates.
- Code cleanup, optimization and refactoring.

**Full Changelog: [1.2.0 -> 1.3.0](https://github.com/OpenShock/Firmware/compare/1.2.0...1.3.0)**




# Version 1.2.0 Release Notes

This release adds a new shocker protocol, more bugfixes, configurability, and performance improvements.

### Highlight

- Added support for **998DR** Petrainer RF protocol.

### Major Updates

- Add command to get/set api domain.
- Add command to get/set/clear override for Live Control Gateway (LCG) domain.

### Minor Updates

- Change transmission end command to last for 300 ms.
- Increase WDT timeout during OTA updates to prevent watchdog resets.
- Remove non thread-safe RF sequence caching.
- Update flatbuffers to 23.5.26.
- Start utilizing StringView more to reduce memory and CPU usage.
- Small code cleanup and refactoring.

**Full Changelog: [1.1.2 -> 1.2.0](https://github.com/OpenShock/Firmware/compare/1.1.2...1.2.0)**




# Version 1.1.2 Release Notes

- Add support for OpenShock Core V2 Hardware

**Full Changelog: [1.1.1 -> 1.1.2](https://github.com/OpenShock/Firmware/compare/1.1.1...1.1.2)**




# Version 1.1.1 Release Notes

This release is increases the stability and performance of the firmware, as well as fixes some minor bugs.

### Highlight

- Enabled release builds for improved firmware size, performance, and stability.

### Minor Updates

- Improved RFTransmitter delay logic to wait patiently for commands if it has no transmissions to send.
- Increased RFTransmitter performance margins to avoid command stacking and delays.
- Updated build script to properly identify git-tag triggered github action runs.

### Bug Fixes

- Fixed a bug where the RFTransmitter loop would never delay, causing other tasks running on the same core to completely halt.
- Removed null check on credentials password received in frontend, as null is expected due to sensitive data removal.

**Full Changelog: [1.1.0 -> 1.1.1](https://github.com/OpenShock/Firmware/compare/1.1.0...1.1.1)**




# Version 1.1.0 Release Notes

It's finally here! The 1.1.0 release of OpenShock is now available for download.

This update is packed with some major enhancements and numerous improvements that we believe will improve your experience using your device.

From introducing seamless Over-The-Air updates, adding support for new hardware, to enhancing the overall functionality and stability of the system, we've worked hard to improve upon our last release.

We've also squashed some pesky bugs and made various minor updates to streamline and optimize your experience.

Here's what's new:

### Major Updates

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

### Minor Updates

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

### Bug Fixes

- Resolved issue with WiFi scans getting stuck.
- Fixed connection problems with unsecured networks.
- Altered CommandHandler to use a queue kill message, preventing panic when deleting a mid-listening queue.
- Fixed ESP becoming unresponsive when the looptask would get deleted by captive portal deconstructor due to a missing null check.
- Fixed updateID not being sent with BootStatus message.
- Make OTA update status reporting smoother.
- Fix what firmware boot type firmware reports to server.
- Reduced latency, allocations, and network traffic for reporting wifi network scan results, making the networks instantly available in the frontend and improving the reliability of the captive portal.

**Full Changelog: [1.0.0 -> 1.1.0](https://github.com/OpenShock/Firmware/compare/1.0.0...1.1.0)**




# Version 1.0.0 Release Notes

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
- Added support for having a E-Stop (emergency stop) connected to ESP as a panic button. Thanks to @nullstalgia
- And _much, much_ more behind the scenes, including bugfixes and code cleanup.

**Full Changelog: [v0.8.1 -> 1.0.0](https://github.com/OpenShock/Firmware/compare/v0.8.1...1.0.0)**




# Version v0.8.1
