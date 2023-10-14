import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToDeviceMessage } from '$lib/fbs/open-shock/serialization/local/local-to-device-message';
import { LocalToDeviceMessagePayload } from '$lib/fbs/open-shock/serialization/local/local-to-device-message-payload';
import { WifiNetworkDisconnectCommand } from '$lib/fbs/open-shock/serialization/local/wifi-network-disconnect-command';

export function SerializeWifiNetworkDisconnectCommand(): Uint8Array {
  const fbb = new FlatbufferBuilder(32);

  const cmdOffset = WifiNetworkDisconnectCommand.createWifiNetworkDisconnectCommand(fbb, true);

  const payloadOffset = LocalToDeviceMessage.createLocalToDeviceMessage(fbb, LocalToDeviceMessagePayload.WifiNetworkDisconnectCommand, cmdOffset);

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
