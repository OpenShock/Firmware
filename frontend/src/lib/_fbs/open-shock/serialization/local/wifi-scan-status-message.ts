// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'flatbuffers';

import { WifiScanStatus } from '../../../open-shock/serialization/types/wifi-scan-status';


export class WifiScanStatusMessage {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):WifiScanStatusMessage {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsWifiScanStatusMessage(bb:flatbuffers.ByteBuffer, obj?:WifiScanStatusMessage):WifiScanStatusMessage {
  return (obj || new WifiScanStatusMessage()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsWifiScanStatusMessage(bb:flatbuffers.ByteBuffer, obj?:WifiScanStatusMessage):WifiScanStatusMessage {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new WifiScanStatusMessage()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

status():WifiScanStatus {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? this.bb!.readUint8(this.bb_pos + offset) : WifiScanStatus.Started;
}

static startWifiScanStatusMessage(builder:flatbuffers.Builder) {
  builder.startObject(1);
}

static addStatus(builder:flatbuffers.Builder, status:WifiScanStatus) {
  builder.addFieldInt8(0, status, WifiScanStatus.Started);
}

static endWifiScanStatusMessage(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  return offset;
}

static createWifiScanStatusMessage(builder:flatbuffers.Builder, status:WifiScanStatus):flatbuffers.Offset {
  WifiScanStatusMessage.startWifiScanStatusMessage(builder);
  WifiScanStatusMessage.addStatus(builder, status);
  return WifiScanStatusMessage.endWifiScanStatusMessage(builder);
}
}
