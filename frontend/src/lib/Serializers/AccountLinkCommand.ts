import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToDeviceMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message';
import { LocalToDeviceMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-device-message-payload';
import { AccountLinkCommand } from '$lib/_fbs/open-shock/serialization/local/account-link-command';

export function SerializeAccountLinkCommand(pairCode: string): Uint8Array {
  const fbb = new FlatbufferBuilder(64);

  const pairCodeOffset = fbb.createString(pairCode);

  const cmdOffset = AccountLinkCommand.createAccountLinkCommand(fbb, pairCodeOffset);

  const payloadOffset = LocalToDeviceMessage.createLocalToDeviceMessage(fbb, LocalToDeviceMessagePayload.AccountLinkCommand, cmdOffset);

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
