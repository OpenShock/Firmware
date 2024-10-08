// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'flatbuffers';

export class WifiScanCommand {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):WifiScanCommand {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsWifiScanCommand(bb:flatbuffers.ByteBuffer, obj?:WifiScanCommand):WifiScanCommand {
  return (obj || new WifiScanCommand()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsWifiScanCommand(bb:flatbuffers.ByteBuffer, obj?:WifiScanCommand):WifiScanCommand {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new WifiScanCommand()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

run():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

static startWifiScanCommand(builder:flatbuffers.Builder) {
  builder.startObject(1);
}

static addRun(builder:flatbuffers.Builder, run:boolean) {
  builder.addFieldInt8(0, +run, +false);
}

static endWifiScanCommand(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  return offset;
}

static createWifiScanCommand(builder:flatbuffers.Builder, run:boolean):flatbuffers.Offset {
  WifiScanCommand.startWifiScanCommand(builder);
  WifiScanCommand.addRun(builder, run);
  return WifiScanCommand.endWifiScanCommand(builder);
}
}
