import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
import { WifiNetworkSaveCommand } from '$lib/_fbs/open-shock/serialization/local/wifi-network-save-command';

export function SerializeWifiNetworkSaveCommand(
  ssid: string,
  password: string | null,
  connect: boolean
): Uint8Array {
  const fbb = new FlatbufferBuilder(128);

  const ssidOffset = fbb.createString(ssid);
  let passwordOffset = 0;
  if (password) {
    passwordOffset = fbb.createString(password);
  }

  const cmdOffset = WifiNetworkSaveCommand.createWifiNetworkSaveCommand(
    fbb,
    ssidOffset,
    passwordOffset,
    connect
  );

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(
    fbb,
    LocalToHubMessagePayload.WifiNetworkSaveCommand,
    cmdOffset
  );

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
