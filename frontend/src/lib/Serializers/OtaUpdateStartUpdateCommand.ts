import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
import { OtaUpdateStartUpdateCommand } from '$lib/_fbs/open-shock/serialization/local/ota-update-start-update-command';

export function SerializeOtaUpdateStartUpdateCommand(channel: string, version: string): Uint8Array {
  const fbb = new FlatbufferBuilder(128);

  const channelOffset = fbb.createString(channel);
  const versionOffset = fbb.createString(version);

  const cmdOffset = OtaUpdateStartUpdateCommand.createOtaUpdateStartUpdateCommand(
    fbb,
    channelOffset,
    versionOffset
  );

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(
    fbb,
    LocalToHubMessagePayload.OtaUpdateStartUpdateCommand,
    cmdOffset
  );

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
