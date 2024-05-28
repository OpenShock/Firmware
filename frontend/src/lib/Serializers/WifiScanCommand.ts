import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { WifiScanCommand } from '$lib/_fbs/open-shock/serialization/local/wifi-scan-command';
import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message-payload';

export function SerializeWifiScanCommand(scan: boolean): Uint8Array {
  const fbb = new FlatbufferBuilder(32);

  const cmdOffset = WifiScanCommand.createWifiScanCommand(fbb, scan);

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(fbb, LocalToHubMessagePayload.WifiScanCommand, cmdOffset);

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
