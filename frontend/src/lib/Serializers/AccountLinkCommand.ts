import { AccountLinkCommand } from '$lib/_fbs/open-shock/serialization/local/account-link-command';
import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
import { Builder as FlatbufferBuilder } from 'flatbuffers';

export function SerializeAccountLinkCommand(linkCode: string): Uint8Array {
  const fbb = new FlatbufferBuilder(64);

  const linkCodeOffset = fbb.createString(linkCode);

  const cmdOffset = AccountLinkCommand.createAccountLinkCommand(fbb, linkCodeOffset);

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(
    fbb,
    LocalToHubMessagePayload.AccountLinkCommand,
    cmdOffset
  );

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
