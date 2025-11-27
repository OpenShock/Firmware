import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
import { SetRfTxPinCommand } from '$lib/_fbs/open-shock/serialization/local/set-rf-tx-pin-command';

export function SerializeSetRfTxPinCommand(pin: number): Uint8Array {
  const fbb = new FlatbufferBuilder(64);

  const cmdOffset = SetRfTxPinCommand.createSetRfTxPinCommand(fbb, pin);

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(
    fbb,
    LocalToHubMessagePayload.SetRfTxPinCommand,
    cmdOffset
  );

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
