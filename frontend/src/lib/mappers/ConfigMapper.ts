import { LedMode as FbsLedMode } from '#lib/_fbs/open-shock/serialization/local/led-mode.js';
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

export type LedMode = 'all' | 'critical' | 'off';

export interface LedBrightness {
  present: boolean; // Whether the board has this LED
  critical: number; // Percent of the LED's full output
  normal: number;
}

export interface LedConfig {
  mode: LedMode;
  mono: LedBrightness;
  rgb: LedBrightness;
  rgbWarnAbove: number; // RGB brightness (percent) above which the hub warns
  previewMs: number; // How long a preview holds the LEDs after the last preview message
}

// The settings the portal shows, as the hub reports them in its Ready message.
export interface Config {
  rf: { txPin: number };
  estop: { enabled: boolean; gpioPin: number };
  otaUpdate: OtaUpdateConfig;
  led: LedConfig;
  savedSSIDs: string[];
}

const _ledModes: Record<FbsLedMode, LedMode> = {
  [FbsLedMode.All]: 'all',
  [FbsLedMode.CriticalOnly]: 'critical',
  [FbsLedMode.Off]: 'off',
};

function mapLedConfig(ready: ReadyMessage): LedConfig {
  const led = ready.led();
  if (!led) throw new Error('ready.led is null');

  const mono = led.mono();
  const rgb = led.rgb();
  return {
    mode: _ledModes[led.mode()] ?? 'all',
    mono: { present: led.hasMono(), critical: mono?.critical() ?? 0, normal: mono?.normal() ?? 0 },
    rgb: { present: led.hasRgb(), critical: rgb?.critical() ?? 0, normal: rgb?.normal() ?? 0 },
    rgbWarnAbove: led.rgbWarnAbove(),
    previewMs: led.previewMs(),
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
    led: mapLedConfig(ready),
    savedSSIDs,
  };
}
