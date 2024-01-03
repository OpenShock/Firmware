import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToDeviceMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message';
import { LocalToDeviceMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message-payload';
import { SetRfTxPinCommand } from '$lib/_fbs/open-shock/serialization/local/set-rf-tx-pin-command';

export function SerializeSetRfTxPinCommand(pin: number): Uint8Array {
  const fbb = new FlatbufferBuilder(64);

  const cmdOffset = SetRfTxPinCommand.createSetRfTxPinCommand(fbb, pin);

  const payloadOffset = LocalToDeviceMessage.createLocalToDeviceMessage(fbb, LocalToDeviceMessagePayload.SetRfTxPinCommand, cmdOffset);

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
