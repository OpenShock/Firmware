import { AccessPointProtection } from '#lib/_fbs/open-shock/serialization/local/access-point-protection.js';
import { AccessPointSettings } from '#lib/_fbs/open-shock/serialization/local/access-point-settings.js';
import { LedSettings } from '#lib/_fbs/open-shock/serialization/local/led-settings.js';
import { OtaChannel } from '#lib/_fbs/open-shock/serialization/local/ota-channel.js';
import { OtaSettings } from '#lib/_fbs/open-shock/serialization/local/ota-settings.js';
import { ReadyMessage } from '#lib/_fbs/open-shock/serialization/local/ready-message.js';
import { LedBrightness } from '#lib/_fbs/open-shock/serialization/types/led-brightness.js';
import { LedMode } from '#lib/_fbs/open-shock/serialization/types/led-mode.js';
import { Builder, ByteBuffer } from 'flatbuffers';
import { describe, expect, it } from 'vitest';
import { mapConfig } from './ConfigMapper';

function readBack(b: Builder, root: number): ReadyMessage {
  b.finish(root);
  return ReadyMessage.getRootAsReadyMessage(new ByteBuffer(b.asUint8Array()));
}

// The LED settings of a board with only an RGB LED, the way the hub serializes them
function buildLed(b: Builder): number {
  const rgb = LedBrightness.createLedBrightness(b, 40000, 5140);
  LedSettings.startLedSettings(b);
  LedSettings.addMode(b, LedMode.CriticalOnly);
  LedSettings.addRgb(b, rgb);
  LedSettings.addRgbWarnAbove(b, 19661);
  return LedSettings.endLedSettings(b);
}

// Builds a Ready message the way the hub serializes it.
function buildReady(cdnDomain: string, ssids: string[]): ReadyMessage {
  const b = new Builder(256);

  const domain = b.createString(cdnDomain);
  const ota = OtaSettings.createOtaSettings(b, true, domain, OtaChannel.Beta, 120, false, true);
  const saved = ReadyMessage.createSavedSsidsVector(
    b,
    ssids.map((ssid) => b.createString(ssid))
  );
  const ap = AccessPointSettings.createAccessPointSettings(
    b,
    b.createString(''),
    b.createString('OpenShock-30:30:F9:7B:80:58'),
    AccessPointProtection.DefaultPassword
  );

  const led = buildLed(b);

  ReadyMessage.startReadyMessage(b);
  ReadyMessage.addRfTxPin(b, 15);
  ReadyMessage.addEstopEnabled(b, true);
  ReadyMessage.addEstopGpioPin(b, 13);
  ReadyMessage.addSavedSsids(b, saved);
  ReadyMessage.addOta(b, ota);
  ReadyMessage.addAccessPoint(b, ap);
  ReadyMessage.addLed(b, led);
  return readBack(b, ReadyMessage.endReadyMessage(b));
}

describe('mapConfig', () => {
  it('maps every setting', () => {
    expect(mapConfig(buildReady('fw.example.org', ['HomeNet', 'Cafe']))).toEqual({
      rf: { txPin: 15 },
      estop: { enabled: true, gpioPin: 13 },
      otaUpdate: {
        isEnabled: true,
        cdnDomain: 'fw.example.org',
        updateChannel: OtaChannel.Beta,
        checkInterval: 120,
        allowBackendManagement: false,
        requireManualApproval: true,
      },
      accessPoint: {
        ssid: null,
        defaultSsid: 'OpenShock-30:30:F9:7B:80:58',
        protection: 'default',
      },
      savedSSIDs: ['HomeNet', 'Cafe'],
      led: {
        mode: 'critical',
        mono: { present: false, critical: 0, normal: 0 },
        rgb: { present: true, critical: 40000, normal: 5140 },
        rgbWarnAbove: 19661,
      },
    });
  });

  it('maps no access point settings from older firmware to null', () => {
    const b = new Builder(64);
    const domain = b.createString('fw.example.org');
    const ota = OtaSettings.createOtaSettings(b, true, domain, OtaChannel.Stable, 30, true, false);
    ReadyMessage.startReadyMessage(b);
    ReadyMessage.addOta(b, ota);

    expect(mapConfig(readBack(b, ReadyMessage.endReadyMessage(b))).accessPoint).toBeNull();
  });

  it('accepts an empty domain and no saved networks', () => {
    const config = mapConfig(buildReady('', []));

    expect(config.otaUpdate.cdnDomain).toBe('');
    expect(config.savedSSIDs).toEqual([]);
  });

  it('reads an unset pin as -1', () => {
    const b = new Builder(64);
    const domain = b.createString('fw.example.org');
    const ota = OtaSettings.createOtaSettings(b, true, domain, OtaChannel.Stable, 30, true, false);
    const led = buildLed(b);
    ReadyMessage.startReadyMessage(b);
    ReadyMessage.addOta(b, ota);
    ReadyMessage.addLed(b, led);

    const config = mapConfig(readBack(b, ReadyMessage.endReadyMessage(b)));

    expect(config.rf.txPin).toBe(-1);
    expect(config.estop.gpioPin).toBe(-1);
  });

  it('maps a mono LED table as present', () => {
    const b = new Builder(64);
    const domain = b.createString('fw.example.org');
    const ota = OtaSettings.createOtaSettings(b, true, domain, OtaChannel.Stable, 30, true, false);
    const mono = LedBrightness.createLedBrightness(b, 30000, 2000);
    LedSettings.startLedSettings(b);
    LedSettings.addMode(b, LedMode.All);
    LedSettings.addMono(b, mono);
    const led = LedSettings.endLedSettings(b);
    ReadyMessage.startReadyMessage(b);
    ReadyMessage.addOta(b, ota);
    ReadyMessage.addLed(b, led);

    const config = mapConfig(readBack(b, ReadyMessage.endReadyMessage(b)));

    expect(config.led?.mono).toEqual({ present: true, critical: 30000, normal: 2000 });
    expect(config.led?.rgb.present).toBe(false);
  });

  it('loads the other settings from firmware without LED settings', () => {
    const b = new Builder(64);
    const domain = b.createString('fw.example.org');
    const ota = OtaSettings.createOtaSettings(b, true, domain, OtaChannel.Stable, 30, true, false);
    ReadyMessage.startReadyMessage(b);
    ReadyMessage.addOta(b, ota);

    const config = mapConfig(readBack(b, ReadyMessage.endReadyMessage(b)));

    expect(config.led).toBeNull();
    expect(config.otaUpdate.cdnDomain).toBe('fw.example.org');
  });

  it('rejects a message without the OTA settings', () => {
    const b = new Builder(64);
    ReadyMessage.startReadyMessage(b);

    expect(() => mapConfig(readBack(b, ReadyMessage.endReadyMessage(b)))).toThrow('ready.ota');
  });
});
