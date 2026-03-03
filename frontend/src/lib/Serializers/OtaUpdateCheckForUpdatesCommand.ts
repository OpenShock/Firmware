import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
import { OtaUpdateCheckForUpdatesCommand } from '$lib/_fbs/open-shock/serialization/local/ota-update-check-for-updates-command';

export function SerializeOtaUpdateCheckForUpdatesCommand(channel: string): Uint8Array {
  const fbb = new FlatbufferBuilder(128);

  const channelOffset = fbb.createString(channel);

  const cmdOffset = OtaUpdateCheckForUpdatesCommand.createOtaUpdateCheckForUpdatesCommand(
    fbb,
    channelOffset
  );

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(
    fbb,
    LocalToHubMessagePayload.OtaUpdateCheckForUpdatesCommand,
    cmdOffset
  );

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
