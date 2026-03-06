import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
import { SetEstopPinCommand } from '$lib/_fbs/open-shock/serialization/local/set-estop-pin-command';
import { Builder as FlatbufferBuilder } from 'flatbuffers';

export function SerializeSetEstopPinCommand(pin: number): Uint8Array {
  const fbb = new FlatbufferBuilder(64);

  const cmdOffset = SetEstopPinCommand.createSetEstopPinCommand(fbb, pin);

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(
    fbb,
    LocalToHubMessagePayload.SetEstopPinCommand,
    cmdOffset
  );

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
