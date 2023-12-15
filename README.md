# OpenShock Espressif Firmware

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
