import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
import { OtaUpdateSetDomainCommand } from '$lib/_fbs/open-shock/serialization/local/ota-update-set-domain-command';

export function SerializeOtaUpdateSetDomainCommand(domain: string): Uint8Array {
  const fbb = new FlatbufferBuilder(128);

  const domainOffset = fbb.createString(domain);

  const cmdOffset = OtaUpdateSetDomainCommand.createOtaUpdateSetDomainCommand(fbb, domainOffset);

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(
    fbb,
    LocalToHubMessagePayload.OtaUpdateSetDomainCommand,
    cmdOffset
  );

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
