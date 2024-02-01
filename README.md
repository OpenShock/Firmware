# OpenShock Espressif Firmware

[![Documentation](https://img.shields.io/badge/docs-mkdocs-blue.svg)](https://openshock.org)
[![GitHub license](https://img.shields.io/github/license/openshock/firmware.svg)](https://raw.githubusercontent.com/openshock/firmware/master/LICENSE)
[![GitHub Releases](https://img.shields.io/github/release/openshock/firmware.svg)](https://github.com/openshock/firmware/releases)
[![GitHub Downloads](https://img.shields.io/github/downloads/openshock/firmware/total)](https://github.com/openshock/firmware/releases)
[![GitHub Sponsors](https://img.shields.io/badge/GitHub-Sponsors-ff69b4)](https://github.com/sponsors/openshock)
[![Discord](https://img.shields.io/discord/1078124408775901204)](https://discord.gg/openshock)

|         |                                                                                                                                                                                        |                                                                                                                                                                                     |                                                                                                                                                                    |
|---------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Master  | [![Build Status](https://github.com/OpenShock/Firmware/actions/workflows/ci-build.yml/badge.svg?branch=master)](https://github.com/OpenShock/Firmware/actions/workflows/ci-build.yml)  | [![CodeQL Status](https://github.com/OpenShock/Firmware/actions/workflows/codeql.yml/badge.svg?branch=master)](https://github.com/OpenShock/Firmware/actions/workflows/codeql.yml)  | [![Coverage Status](https://coveralls.io/repos/github/openshock/firmware/badge.svg?branch=master)](https://coveralls.io/github/openshock/firmware?branch=master)   |
| Beta    | [![Build Status](https://github.com/OpenShock/Firmware/actions/workflows/ci-build.yml/badge.svg?branch=beta)](https://github.com/OpenShock/Firmware/actions/workflows/ci-build.yml)    | [![CodeQL Status](https://github.com/OpenShock/Firmware/actions/workflows/codeql.yml/badge.svg?branch=beta)](https://github.com/OpenShock/Firmware/actions/workflows/codeql.yml)    | [![Coverage Status](https://coveralls.io/repos/github/openshock/firmware/badge.svg?branch=beta)](https://coveralls.io/github/openshock/firmware?branch=beta)       |
| Develop | [![Build Status](https://github.com/OpenShock/Firmware/actions/workflows/ci-build.yml/badge.svg?branch=develop)](https://github.com/OpenShock/Firmware/actions/workflows/ci-build.yml) | [![CodeQL Status](https://github.com/OpenShock/Firmware/actions/workflows/codeql.yml/badge.svg?branch=develop)](https://github.com/OpenShock/Firmware/actions/workflows/codeql.yml) | [![Coverage Status](https://coveralls.io/repos/github/openshock/firmware/badge.svg?branch=develop)](https://coveralls.io/github/openshock/firmware?branch=develop) |

Espressif Firmware for OpenShock.

Controlling shockers via Reverse engineered proprietary Sub-1 GHz Protocols.

## Compatible Hardware

You will need a ESP-32 and a 433 MHz antenna attached to it.

For more info about buying such hardware see here [OpenShock Wiki - Vendors: Hardware](https://wiki.openshock.org/vendors/hardware/).

Guide for assembly can be found here [OpenShock Wiki - DIY: Assembling](https://wiki.openshock.org/diy/assembling/)

Confirmed working boards:

- PiShock
  - 2021 Q3
  - 2023
- Seeed
  - Xiao ESP32S3
- Wemos
  - D1 Mini
  - Lolin S2 Mini
  - Lolin S3
- OpenShock (Legacy)
  - Core V1

## Flashing

Refer to [OpenShock Wiki - Guides: First time setup](https://wiki.openshock.org/guides/openshock-first-setup/) on how to set up your microcontroller.

Other than that, you can just flash via platform io in vscode. More in the contribute section.

## Contribute

You will need:

- VSCode
- Knowledge about Arduino library and C++
- Optimally compatible hardware to test your code

### Setting up

```bash
# Install dependencies
pip install -r requirements.txt
```
