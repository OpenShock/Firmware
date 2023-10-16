import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToDeviceMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message';
import { LocalToDeviceMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message-payload';
import { GatewayPairCommand } from '$lib/_fbs/open-shock/serialization/local/gateway-pair-command';

export function SerializeGatewayPairCommand(pairCode: string): Uint8Array {
  const fbb = new FlatbufferBuilder(64);

  const pairCodeOffset = fbb.createString(pairCode);

  const cmdOffset = GatewayPairCommand.createGatewayPairCommand(fbb, pairCodeOffset);

  const payloadOffset = LocalToDeviceMessage.createLocalToDeviceMessage(fbb, LocalToDeviceMessagePayload.GatewayPairCommand, cmdOffset);

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
