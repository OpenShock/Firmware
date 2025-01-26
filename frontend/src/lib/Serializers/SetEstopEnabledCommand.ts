import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
import { SetEstopEnabledCommand } from '$lib/_fbs/open-shock/serialization/local/set-estop-enabled-command';

export function SerializeSetEstopEnabledCommand(enabled: boolean): Uint8Array {
  const fbb = new FlatbufferBuilder(64);

  const cmdOffset = SetEstopEnabledCommand.createSetEstopEnabledCommand(fbb, enabled);

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(
    fbb,
    LocalToHubMessagePayload.SetEstopEnabledCommand,
    cmdOffset
  );

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
