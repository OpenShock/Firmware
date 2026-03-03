import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
import { OtaUpdateSetCheckIntervalCommand } from '$lib/_fbs/open-shock/serialization/local/ota-update-set-check-interval-command';

export function SerializeOtaUpdateSetCheckIntervalCommand(interval: number): Uint8Array {
  const fbb = new FlatbufferBuilder(64);

  const cmdOffset = OtaUpdateSetCheckIntervalCommand.createOtaUpdateSetCheckIntervalCommand(
    fbb,
    interval
  );

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(
    fbb,
    LocalToHubMessagePayload.OtaUpdateSetCheckIntervalCommand,
    cmdOffset
  );

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
