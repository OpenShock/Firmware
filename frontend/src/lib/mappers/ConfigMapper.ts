import type { LedSettings } from '#lib/_fbs/open-shock/serialization/local/led-settings.js';
import type { OtaChannel } from '#lib/_fbs/open-shock/serialization/local/ota-channel.js';
import type { ReadyMessage } from '#lib/_fbs/open-shock/serialization/local/ready-message.js';
import type { LedBrightness as LedBrightnessTable } from '#lib/_fbs/open-shock/serialization/types/led-brightness.js';
import { LedMode as FbsLedMode } from '#lib/_fbs/open-shock/serialization/types/led-mode.js';

export interface OtaUpdateConfig {
  isEnabled: boolean;
  cdnDomain: string;
  updateChannel: OtaChannel;
  checkInterval: number;
  allowBackendManagement: boolean;
  requireManualApproval: boolean;
}

export type LedMode = 'all' | 'critical' | 'off';

export interface LedBrightness {
  present: boolean; // Whether the board has this LED
  critical: number; // 16-bit, 0-65535 of the LED's full output
  normal: number;
}

export interface LedConfig {
  mode: LedMode;
  mono: LedBrightness;
  rgb: LedBrightness;
  rgbWarnAbove: number; // RGB brightness (0-65535) above which the hub warns
}

// The settings the portal shows, as the hub reports them in its Ready message.
export interface Config {
  rf: { txPin: number };
  estop: { enabled: boolean; gpioPin: number };
  otaUpdate: OtaUpdateConfig;
  led: LedConfig | null; // Null from firmware older than this frontend (e.g. after an interrupted update)
  savedSSIDs: string[];
}

const _ledModes: Record<FbsLedMode, LedMode> = {
  [FbsLedMode.All]: 'all',
  [FbsLedMode.CriticalOnly]: 'critical',
  [FbsLedMode.Off]: 'off',
};

// From the Ready message, or a LedSettingsChangedEvent
export function mapLedConfig(led: LedSettings): LedConfig {
  // The hub only sends the LEDs the board has
  const mapBrightness = (b: LedBrightnessTable | null): LedBrightness => ({
    present: b !== null,
    critical: b?.critical() ?? 0,
    normal: b?.normal() ?? 0,
  });
  return {
    mode: _ledModes[led.mode()] ?? 'all',
    mono: mapBrightness(led.mono()),
    rgb: mapBrightness(led.rgb()),
    rgbWarnAbove: led.rgbWarnAbove(),
  };
}

function mapOptionalLed(led: LedSettings | null): LedConfig | null {
  return led ? mapLedConfig(led) : null;
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
    // Optional, so the other settings still load from firmware that has no LED settings
    led: mapOptionalLed(ready.led()),
    savedSSIDs,
  };
}
