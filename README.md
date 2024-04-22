# OpenShock Espressif Firmware

[![Documentation](https://img.shields.io/badge/docs-mkdocs-blue.svg)](https://openshock.org)
[![GitHub license](https://img.shields.io/github/license/openshock/firmware.svg)](https://raw.githubusercontent.com/openshock/firmware/master/LICENSE)
[![GitHub Releases](https://img.shields.io/github/release/openshock/firmware.svg)](https://github.com/openshock/firmware/releases)
[![GitHub Downloads](https://img.shields.io/github/downloads/openshock/firmware/total)](https://github.com/openshock/firmware/releases)
[![GitHub Sponsors](https://img.shields.io/badge/GitHub-Sponsors-ff69b4)](https://github.com/sponsors/openshock)
[![Discord](https://img.shields.io/discord/1078124408775901204)](https://discord.gg/openshock)

<table>
  <tr>
    <td>master</td>
    <td><a href="https://github.com/OpenShock/Firmware/actions/workflows/ci-build.yml"><img src="https://github.com/OpenShock/Firmware/actions/workflows/ci-build.yml/badge.svg?branch=master" alt="Build Status" /></a></td>
    <td><a href="https://github.com/OpenShock/Firmware/actions/workflows/codeql.yml"><img src="https://github.com/OpenShock/Firmware/actions/workflows/codeql.yml/badge.svg?branch=master" alt="CodeQL Status" /></a></td>
    <td><a href="https://coveralls.io/github/openshock/firmware?branch=master"><img src="https://coveralls.io/repos/github/openshock/firmware/badge.svg?branch=master" alt="Coverage Status" /></a></td>
  </tr>
  <tr>
    <td>beta</td>
    <td><a href="https://github.com/OpenShock/Firmware/actions/workflows/ci-build.yml"><img src="https://github.com/OpenShock/Firmware/actions/workflows/ci-build.yml/badge.svg?branch=beta" alt="Build Status" /></a></td>
    <td><a href="https://github.com/OpenShock/Firmware/actions/workflows/codeql.yml"><img src="https://github.com/OpenShock/Firmware/actions/workflows/codeql.yml/badge.svg?branch=beta" alt="CodeQL Status" /></a></td>
    <td><a href="https://coveralls.io/github/openshock/firmware?branch=master"><img src="https://coveralls.io/repos/github/openshock/firmware/badge.svg?branch=beta" alt="Coverage Status" /></a></td>
  </tr>
  <tr>
    <td>develop</td>
    <td><a href="https://github.com/OpenShock/Firmware/actions/workflows/ci-build.yml"><img src="https://github.com/OpenShock/Firmware/actions/workflows/ci-build.yml/badge.svg?branch=develop" alt="Build Status" /></a></td>
    <td><a href="https://github.com/OpenShock/Firmware/actions/workflows/codeql.yml"><img src="https://github.com/OpenShock/Firmware/actions/workflows/codeql.yml/badge.svg?branch=develop" alt="CodeQL Status" /></a></td>
    <td><a href="https://coveralls.io/github/openshock/firmware?branch=master"><img src="https://coveralls.io/repos/github/openshock/firmware/badge.svg?branch=develop" alt="Coverage Status" /></a></td>
  </tr>
</table>

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
- Compatible ESP-32 board with 433 MHz antenna

### Setting up

```bash
# Install dependencies
pip install -r requirements.txt
```
