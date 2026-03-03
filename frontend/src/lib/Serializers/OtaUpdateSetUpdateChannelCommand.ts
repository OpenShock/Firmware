import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
import { OtaUpdateSetUpdateChannelCommand } from '$lib/_fbs/open-shock/serialization/local/ota-update-set-update-channel-command';

export function SerializeOtaUpdateSetUpdateChannelCommand(channel: string): Uint8Array {
  const fbb = new FlatbufferBuilder(128);

  const channelOffset = fbb.createString(channel);

  const cmdOffset = OtaUpdateSetUpdateChannelCommand.createOtaUpdateSetUpdateChannelCommand(
    fbb,
    channelOffset
  );

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(
    fbb,
    LocalToHubMessagePayload.OtaUpdateSetUpdateChannelCommand,
    cmdOffset
  );

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
