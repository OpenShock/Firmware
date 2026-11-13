# Security Policy

OpenShock controls devices that people wear, so we take security reports seriously.
This file covers what is specific to the OpenShock firmware. Everything else, including scope, rules for
testing, response times, disclosure and safe harbor, is in the
**[OpenShock security policy](https://github.com/OpenShock/.github/blob/main/SECURITY.md)**.
Please read it before testing.

## Reporting a vulnerability

**Do not open a public issue, pull request or Discord thread for a security vulnerability.**

Report it privately through one of these channels:

- [GitHub private vulnerability reporting](https://github.com/OpenShock/Firmware/security/advisories/new) (preferred)
- Email to [security@openshock.org](mailto:security@openshock.org)

We acknowledge reports within 3 working days (7 days at most), give a triage result within
14 days, and follow coordinated disclosure with a default deadline of 90 days. OpenShock does not
pay bounties; reporters are credited in the advisory and in our
[security acknowledgments](https://github.com/OpenShock/.github/blob/main/SECURITY-ACKNOWLEDGMENTS.md)
if they wish.

## Supported versions

| Version                                                                 | Receives security fixes                                          |
| ----------------------------------------------------------------------- | ---------------------------------------------------------------- |
| Latest stable [release](https://github.com/OpenShock/Firmware/releases) | Yes                                                              |
| Older stable releases                                                   | No, update to the latest release                                 |
| Beta and develop builds                                                 | Handled as regular bugs unless a stable release is also affected |

Security fixes are shipped as a new firmware release for all
[maintained boards](https://wiki.openshock.org/hardware/boards/#fully-maintained) and are
delivered through the normal update mechanism: over-the-air update or the web flasher. They are
announced in the release notes and as a GitHub Security Advisory in this repository.

Modified builds, forks and boards that are not on the maintained list are not covered.

## What we are most interested in

- Anything that lets a remote attacker trigger a shocker, or take over a hub, without being
  authorized through the backend.
- The over-the-air update path: installing firmware from an untrusted source, downgrade attacks,
  bypassing integrity checks.
- TLS and certificate validation towards the backend and gateway.
- The captive portal and local setup interface: flaws exploitable by someone on the same network
  or within Wi-Fi range.
- Exposure of stored secrets (Wi-Fi credentials, hub token) to a remote or nearby attacker.
- Memory safety bugs reachable from the network, the captive portal or received radio data.
- Bypassing safety limits enforced by the firmware.

## Testing

**Only test on hardware you own.** Firmware issues can be researched entirely on your own hub
and, if needed, against your own backend. Follow the
[safety rules](https://wiki.openshock.org/home/safety-rules) when a shocker is connected, and do
not test on a device that someone is wearing.

## Not considered vulnerabilities in this repository

- The radio protocols of supported shockers. They are proprietary protocols of the hardware
  manufacturers without encryption or replay protection, which OpenShock cannot fix.
- Attacks that require physical access to the hub, such as reflashing over USB, reading the
  flash, or using the serial console.
- Radio jamming and other denial of service by someone within radio range.
- Hardware and vendor firmware (bootloader, ESP-IDF, radio modules) unless OpenShock uses them
  insecurely. Report those to the vendor.
