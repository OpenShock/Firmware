import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToDeviceMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message';
import { LocalToDeviceMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message-payload';
import { SetEstopPinCommand } from '$lib/_fbs/open-shock/serialization/local/set-estop-pin-command';

export function SerializeSetEstopPinCommand(pin: number): Uint8Array {
  const fbb = new FlatbufferBuilder(64);

  const cmdOffset = SetEstopPinCommand.createSetEstopPinCommand(fbb, pin);

  const payloadOffset = LocalToDeviceMessage.createLocalToDeviceMessage(fbb, LocalToDeviceMessagePayload.SetEstopPinCommand, cmdOffset);

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
