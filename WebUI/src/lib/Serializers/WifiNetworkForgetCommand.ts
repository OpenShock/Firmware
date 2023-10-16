import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToDeviceMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message';
import { LocalToDeviceMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message-payload';
import { WifiNetworkForgetCommand } from '$lib/_fbs/open-shock/serialization/local/wifi-network-forget-command';

export function SerializeWifiNetworkForgetCommand(ssid: string): Uint8Array {
  const fbb = new FlatbufferBuilder(128);

  const ssidOffset = fbb.createString(ssid);

  const cmdOffset = WifiNetworkForgetCommand.createWifiNetworkForgetCommand(fbb, ssidOffset);

  const payloadOffset = LocalToDeviceMessage.createLocalToDeviceMessage(fbb, LocalToDeviceMessagePayload.WifiNetworkForgetCommand, cmdOffset);

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
