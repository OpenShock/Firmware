import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToDeviceMessage } from '$lib/fbs/open-shock/serialization/local/local-to-device-message';
import { LocalToDeviceMessagePayload } from '$lib/fbs/open-shock/serialization/local/local-to-device-message-payload';
import { WifiNetworkForgetCommand } from '$lib/fbs/open-shock/serialization/local/wifi-network-forget-command';

export function SerializeWifiNetworkForgetCommand(ssid: string, bssid: string): Uint8Array {
  const fbb = new FlatbufferBuilder(128);

  const ssidOffset = fbb.createString(ssid);
  const bssidOffset = fbb.createString(bssid);

  const cmdOffset = WifiNetworkForgetCommand.createWifiNetworkForgetCommand(fbb, ssidOffset, bssidOffset);

  const payloadOffset = LocalToDeviceMessage.createLocalToDeviceMessage(fbb, LocalToDeviceMessagePayload.WifiNetworkForgetCommand, cmdOffset);

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
