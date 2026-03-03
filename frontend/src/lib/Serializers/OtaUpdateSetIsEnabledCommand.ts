import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
import { OtaUpdateSetIsEnabledCommand } from '$lib/_fbs/open-shock/serialization/local/ota-update-set-is-enabled-command';

export function SerializeOtaUpdateSetIsEnabledCommand(enabled: boolean): Uint8Array {
  const fbb = new FlatbufferBuilder(64);

  const cmdOffset = OtaUpdateSetIsEnabledCommand.createOtaUpdateSetIsEnabledCommand(fbb, enabled);

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(
    fbb,
    LocalToHubMessagePayload.OtaUpdateSetIsEnabledCommand,
    cmdOffset
  );

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
