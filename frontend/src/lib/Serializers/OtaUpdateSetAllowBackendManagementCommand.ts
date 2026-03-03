import { Builder as FlatbufferBuilder } from 'flatbuffers';
import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
import { OtaUpdateSetAllowBackendManagementCommand } from '$lib/_fbs/open-shock/serialization/local/ota-update-set-allow-backend-management-command';

export function SerializeOtaUpdateSetAllowBackendManagementCommand(allow: boolean): Uint8Array {
  const fbb = new FlatbufferBuilder(64);

  const cmdOffset =
    OtaUpdateSetAllowBackendManagementCommand.createOtaUpdateSetAllowBackendManagementCommand(
      fbb,
      allow
    );

  const payloadOffset = LocalToHubMessage.createLocalToHubMessage(
    fbb,
    LocalToHubMessagePayload.OtaUpdateSetAllowBackendManagementCommand,
    cmdOffset
  );

  fbb.finish(payloadOffset);

  return fbb.asUint8Array();
}
