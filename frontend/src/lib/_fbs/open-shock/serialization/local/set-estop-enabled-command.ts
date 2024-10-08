// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import * as flatbuffers from 'flatbuffers';

export class SetEstopEnabledCommand {
  bb: flatbuffers.ByteBuffer|null = null;
  bb_pos = 0;
  __init(i:number, bb:flatbuffers.ByteBuffer):SetEstopEnabledCommand {
  this.bb_pos = i;
  this.bb = bb;
  return this;
}

static getRootAsSetEstopEnabledCommand(bb:flatbuffers.ByteBuffer, obj?:SetEstopEnabledCommand):SetEstopEnabledCommand {
  return (obj || new SetEstopEnabledCommand()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

static getSizePrefixedRootAsSetEstopEnabledCommand(bb:flatbuffers.ByteBuffer, obj?:SetEstopEnabledCommand):SetEstopEnabledCommand {
  bb.setPosition(bb.position() + flatbuffers.SIZE_PREFIX_LENGTH);
  return (obj || new SetEstopEnabledCommand()).__init(bb.readInt32(bb.position()) + bb.position(), bb);
}

enabled():boolean {
  const offset = this.bb!.__offset(this.bb_pos, 4);
  return offset ? !!this.bb!.readInt8(this.bb_pos + offset) : false;
}

static startSetEstopEnabledCommand(builder:flatbuffers.Builder) {
  builder.startObject(1);
}

static addEnabled(builder:flatbuffers.Builder, enabled:boolean) {
  builder.addFieldInt8(0, +enabled, +false);
}

static endSetEstopEnabledCommand(builder:flatbuffers.Builder):flatbuffers.Offset {
  const offset = builder.endObject();
  return offset;
}

static createSetEstopEnabledCommand(builder:flatbuffers.Builder, enabled:boolean):flatbuffers.Offset {
  SetEstopEnabledCommand.startSetEstopEnabledCommand(builder);
  SetEstopEnabledCommand.addEnabled(builder, enabled);
  return SetEstopEnabledCommand.endSetEstopEnabledCommand(builder);
}
}