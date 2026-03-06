import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
import { WifiNetworkDisconnectCommand } from '$lib/_fbs/open-shock/serialization/local/wifi-network-disconnect-command';
import { Builder as FlatbufferBuilder } from 'flatbuffers';

export function SerializeWifiNetworkDisconnectCommand(): Uint8Array {
  const fbb = new FlatbufferBuilder(32);

  const cmdOffset = WifiNetworkDisconnectCommand.createWifiNetworkDisconnectCommand(fbb, true);

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(
    fbb,
    LocalToHubMessagePayload.WifiNetworkDisconnectCommand,
    cmdOffset
  );

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
