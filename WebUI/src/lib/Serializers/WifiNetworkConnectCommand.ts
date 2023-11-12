import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToDeviceMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message';
import { LocalToDeviceMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message-payload';
import { WifiNetworkConnectCommand } from '$lib/_fbs/open-shock/serialization/local/wifi-network-connect-command';

export function SerializeWifiNetworkConnectCommand(ssid: string): Uint8Array {
  const fbb = new FlatbufferBuilder(128);

  const ssidOffset = fbb.createString(ssid);

  const cmdOffset = WifiNetworkConnectCommand.createWifiNetworkConnectCommand(fbb, ssidOffset);

  const payloadOffset = LocalToDeviceMessage.createLocalToDeviceMessage(fbb, LocalToDeviceMessagePayload.WifiNetworkConnectCommand, cmdOffset);

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
