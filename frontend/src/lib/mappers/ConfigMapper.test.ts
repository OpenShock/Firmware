import { OtaChannel } from '#lib/_fbs/open-shock/serialization/local/ota-channel.js';
import { OtaSettings } from '#lib/_fbs/open-shock/serialization/local/ota-settings.js';
import { ReadyMessage } from '#lib/_fbs/open-shock/serialization/local/ready-message.js';
import { Builder, ByteBuffer } from 'flatbuffers';
import { describe, expect, it } from 'vitest';
import { mapConfig } from './ConfigMapper';

function readBack(b: Builder, root: number): ReadyMessage {
  b.finish(root);
  return ReadyMessage.getRootAsReadyMessage(new ByteBuffer(b.asUint8Array()));
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

  ReadyMessage.startReadyMessage(b);
  ReadyMessage.addRfTxPin(b, 15);
  ReadyMessage.addEstopEnabled(b, true);
  ReadyMessage.addEstopGpioPin(b, 13);
  ReadyMessage.addSavedSsids(b, saved);
  ReadyMessage.addOta(b, ota);
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
      savedSSIDs: ['HomeNet', 'Cafe'],
    });
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
    ReadyMessage.startReadyMessage(b);
    ReadyMessage.addOta(b, ota);

    const config = mapConfig(readBack(b, ReadyMessage.endReadyMessage(b)));

    expect(config.rf.txPin).toBe(-1);
    expect(config.estop.gpioPin).toBe(-1);
  });

  it('rejects a message without the OTA settings', () => {
    const b = new Builder(64);
    ReadyMessage.startReadyMessage(b);

    expect(() => mapConfig(readBack(b, ReadyMessage.endReadyMessage(b)))).toThrow('ready.ota');
  });
});
