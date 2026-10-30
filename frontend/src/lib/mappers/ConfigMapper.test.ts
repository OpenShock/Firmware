import { BackendConfig } from '#lib/_fbs/open-shock/serialization/configuration/backend-config.js';
import { CaptivePortalConfig } from '#lib/_fbs/open-shock/serialization/configuration/captive-portal-config.js';
import { EStopConfig } from '#lib/_fbs/open-shock/serialization/configuration/estop-config.js';
import { HubConfig } from '#lib/_fbs/open-shock/serialization/configuration/hub-config.js';
import { OtaUpdateChannel } from '#lib/_fbs/open-shock/serialization/configuration/ota-update-channel.js';
import { OtaUpdateConfig } from '#lib/_fbs/open-shock/serialization/configuration/ota-update-config.js';
import { OtaUpdateStep } from '#lib/_fbs/open-shock/serialization/configuration/ota-update-step.js';
import { RFConfig } from '#lib/_fbs/open-shock/serialization/configuration/rfconfig.js';
import { SerialInputConfig } from '#lib/_fbs/open-shock/serialization/configuration/serial-input-config.js';
import { WiFiConfig } from '#lib/_fbs/open-shock/serialization/configuration/wi-fi-config.js';
import { WiFiCredentials } from '#lib/_fbs/open-shock/serialization/configuration/wi-fi-credentials.js';
import { Builder, ByteBuffer } from 'flatbuffers';
import { describe, expect, it } from 'vitest';
import { mapConfig } from './ConfigMapper';

interface Strings {
  apSsid: string;
  hostname: string;
  domain: string;
  cdnDomain: string;
}

// Builds a HubConfig the way the hub serializes it (every section present).
function buildHubConfig(strings: Strings): HubConfig {
  const b = new Builder(512);

  const ssid = b.createString('HomeNet');
  WiFiCredentials.startWiFiCredentials(b);
  WiFiCredentials.addId(b, 1);
  WiFiCredentials.addSsid(b, ssid);
  const cred = WiFiCredentials.endWiFiCredentials(b);

  const wifi = WiFiConfig.createWiFiConfig(
    b,
    b.createString(strings.apSsid),
    b.createString(strings.hostname),
    WiFiConfig.createCredentialsVector(b, [cred]),
    0,
    false,
    false,
    false
  );
  const rf = RFConfig.createRFConfig(b, 15, true);
  const portal = CaptivePortalConfig.createCaptivePortalConfig(b, false);
  const backend = BackendConfig.createBackendConfig(b, b.createString(strings.domain), 0);
  const serial = SerialInputConfig.createSerialInputConfig(b, true);
  const ota = OtaUpdateConfig.createOtaUpdateConfig(
    b,
    true,
    b.createString(strings.cdnDomain),
    OtaUpdateChannel.Beta,
    true,
    false,
    30,
    true,
    false,
    0,
    OtaUpdateStep.None
  );
  const estop = EStopConfig.createEStopConfig(b, true, 13, false, false);

  HubConfig.startHubConfig(b);
  HubConfig.addRf(b, rf);
  HubConfig.addWifi(b, wifi);
  HubConfig.addCaptivePortal(b, portal);
  HubConfig.addBackend(b, backend);
  HubConfig.addSerialInput(b, serial);
  HubConfig.addOtaUpdate(b, ota);
  HubConfig.addEstop(b, estop);
  HubConfig.finishHubConfigBuffer(b, HubConfig.endHubConfig(b));

  return HubConfig.getRootAsHubConfig(new ByteBuffer(b.asUint8Array()));
}

describe('mapConfig', () => {
  it('returns null for a missing config', () => {
    expect(mapConfig(null)).toBeNull();
  });

  it('maps every section', () => {
    const config = mapConfig(
      buildHubConfig({
        apSsid: 'OpenShock-',
        hostname: 'hub',
        domain: 'api.example.org',
        cdnDomain: 'fw.example.org',
      })
    );

    expect(config?.rf).toEqual({ txPin: 15, keepaliveEnabled: true });
    expect(config?.wifi.hostname).toBe('hub');
    expect(config?.wifi.credentials).toEqual([{ id: 1, ssid: 'HomeNet', password: null }]);
    expect(config?.backend.domain).toBe('api.example.org');
    expect(config?.otaUpdate.cdnDomain).toBe('fw.example.org');
    expect(config?.otaUpdate.updateChannel).toBe(OtaUpdateChannel.Beta);
    expect(config?.estop).toEqual({ enabled: true, gpioPin: 13, latching: false, active: false });
  });

  it('accepts empty strings the hub can legitimately store', () => {
    const config = mapConfig(
      buildHubConfig({ apSsid: '', hostname: '', domain: '', cdnDomain: '' })
    );

    expect(config?.wifi.apSsid).toBe('');
    expect(config?.backend.domain).toBe('');
    expect(config?.otaUpdate.cdnDomain).toBe('');
  });
});
