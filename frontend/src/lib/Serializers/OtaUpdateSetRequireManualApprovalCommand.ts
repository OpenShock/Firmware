import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
import { OtaUpdateSetRequireManualApprovalCommand } from '$lib/_fbs/open-shock/serialization/local/ota-update-set-require-manual-approval-command';

export function SerializeOtaUpdateSetRequireManualApprovalCommand(require: boolean): Uint8Array {
  const fbb = new FlatbufferBuilder(64);

  const cmdOffset =
    OtaUpdateSetRequireManualApprovalCommand.createOtaUpdateSetRequireManualApprovalCommand(
      fbb,
      require
    );

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(
    fbb,
    LocalToHubMessagePayload.OtaUpdateSetRequireManualApprovalCommand,
    cmdOffset
  );

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
