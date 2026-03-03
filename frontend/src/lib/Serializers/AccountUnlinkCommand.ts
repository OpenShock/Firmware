import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
import { AccountUnlinkCommand } from '$lib/_fbs/open-shock/serialization/local/account-unlink-command';

export function SerializeAccountUnlinkCommand(): Uint8Array {
  const fbb = new FlatbufferBuilder(64);

  const cmdOffset = AccountUnlinkCommand.createAccountUnlinkCommand(fbb, false);

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(
    fbb,
    LocalToHubMessagePayload.AccountUnlinkCommand,
    cmdOffset
  );

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
