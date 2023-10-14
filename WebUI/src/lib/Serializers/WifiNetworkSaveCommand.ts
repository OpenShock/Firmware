import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToDeviceMessage } from '$lib/fbs/open-shock/serialization/local/local-to-device-message';
import { LocalToDeviceMessagePayload } from '$lib/fbs/open-shock/serialization/local/local-to-device-message-payload';
import { WifiNetworkSaveCommand } from '$lib/fbs/open-shock/serialization/local/wifi-network-save-command';

export function SerializeWifiNetworkSaveCommand(ssid: string, bssid: string, password: string): Uint8Array {
  const fbb = new FlatbufferBuilder(128);

  const ssidOffset = fbb.createString(ssid);
  const bssidOffset = fbb.createString(bssid);
  const passwordOffset = fbb.createString(password);

  const cmdOffset = WifiNetworkSaveCommand.createWifiNetworkSaveCommand(fbb, ssidOffset, bssidOffset, passwordOffset);

  const payloadOffset = LocalToDeviceMessage.createLocalToDeviceMessage(fbb, LocalToDeviceMessagePayload.WifiNetworkSaveCommand, cmdOffset);

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
