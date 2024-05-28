import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message-payload';
import { WifiNetworkConnectCommand } from '$lib/_fbs/open-shock/serialization/local/wifi-network-connect-command';

export function SerializeWifiNetworkConnectCommand(ssid: string): Uint8Array {
  const fbb = new FlatbufferBuilder(128);

  const ssidOffset = fbb.createString(ssid);

  const cmdOffset = WifiNetworkConnectCommand.createWifiNetworkConnectCommand(fbb, ssidOffset);

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(fbb, LocalToHubMessagePayload.WifiNetworkConnectCommand, cmdOffset);

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
