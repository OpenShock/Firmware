import { AccessPointProtection } from '#lib/_fbs/open-shock/serialization/local/access-point-protection.js';
import type { OtaChannel } from '#lib/_fbs/open-shock/serialization/local/ota-channel.js';
import type { ReadyMessage } from '#lib/_fbs/open-shock/serialization/local/ready-message.js';

export interface OtaUpdateConfig {
  isEnabled: boolean;
  cdnDomain: string;
  updateChannel: OtaChannel;
  checkInterval: number;
  allowBackendManagement: boolean;
  requireManualApproval: boolean;
}

export type AccessPointProtectionKind = 'default' | 'open' | 'custom';

// The captive portal's access point as saved; the password never leaves the hub
export interface AccessPointConfig {
  ssid: string | null; // The name set for it; null for the default
  defaultSsid: string; // The default name: the prefix with the hub's MAC
  protection: AccessPointProtectionKind;
}

// The settings the portal shows, as the hub reports them in its Ready message.
export interface Config {
  rf: { txPin: number };
  estop: { enabled: boolean; gpioPin: number };
  otaUpdate: OtaUpdateConfig;
  accessPoint: AccessPointConfig | null; // Null from firmware older than this frontend
  savedSSIDs: string[];
}

const _protections: Record<AccessPointProtection, AccessPointProtectionKind> = {
  [AccessPointProtection.Open]: 'open',
  [AccessPointProtection.DefaultPassword]: 'default',
  [AccessPointProtection.CustomPassword]: 'custom',
};

function mapAccessPointConfig(ready: ReadyMessage): AccessPointConfig | null {
  const ap = ready.accessPoint();
  if (!ap) return null;
  return {
    ssid: ap.ssid() || null,
    defaultSsid: ap.defaultSsid() ?? '',
    // An unknown protection is some password: never claim the network is open
    protection: _protections[ap.protection()] ?? 'custom',
  };
}

function mapOtaUpdateConfig(ready: ReadyMessage): OtaUpdateConfig {
  const ota = ready.ota();
  if (!ota) throw new Error('ready.ota is null');

  const cdnDomain = ota.cdnDomain();
  if (cdnDomain === null) throw new Error('ota.cdnDomain is null');

  return {
    isEnabled: ota.enabled(),
    cdnDomain,
    updateChannel: ota.channel(),
    checkInterval: ota.checkInterval(),
    allowBackendManagement: ota.allowBackendManagement(),
    requireManualApproval: ota.requireManualApproval(),
  };
}

export function mapConfig(ready: ReadyMessage): Config {
  const savedSSIDs: string[] = [];
  for (let i = 0; i < ready.savedSsidsLength(); i++) {
    savedSSIDs.push(ready.savedSsids(i));
  }

  return {
    rf: { txPin: ready.rfTxPin() },
    estop: { enabled: ready.estopEnabled(), gpioPin: ready.estopGpioPin() },
    otaUpdate: mapOtaUpdateConfig(ready),
    accessPoint: mapAccessPointConfig(ready),
    savedSSIDs,
  };
}
