import { LedOverrideEnd } from '#lib/_fbs/open-shock/serialization/local/led-override-end.js';
import { LedOverrideEnded } from '#lib/_fbs/open-shock/serialization/local/led-override-ended.js';
import { LedOverride } from '#lib/_fbs/open-shock/serialization/local/led-override.js';
import { LedRequestError } from '#lib/_fbs/open-shock/serialization/local/led-request-error.js';
import { LedRequestFailed } from '#lib/_fbs/open-shock/serialization/local/led-request-failed.js';
import { LedShowing } from '#lib/_fbs/open-shock/serialization/local/led-showing.js';
import { LedStatusEvent } from '#lib/_fbs/open-shock/serialization/local/led-status-event.js';
import { LedStatus } from '#lib/_fbs/open-shock/serialization/local/led-status.js';
import { LedOverrideLayer } from '#lib/_fbs/open-shock/serialization/types/led-override-layer.js';
import { LedPattern } from '#lib/_fbs/open-shock/serialization/types/led-pattern.js';
import { Builder, ByteBuffer } from 'flatbuffers';
import { describe, expect, it } from 'vitest';
import { mapLedNotice, mapLedStatus } from './LedStatusMapper';

// A status event the way the hub serializes it: a color preview by client 2, a draft by client 1
function buildEvent(extra: (b: Builder) => { ended?: number; failed?: number }): LedStatusEvent {
  const b = new Builder(128);
  const display = LedOverride.createLedOverride(b, 2, 2000);
  const draft = LedOverride.createLedOverride(b, 1, 10000);
  LedStatus.startLedStatus(b);
  LedStatus.addShowing(b, LedShowing.Color);
  LedStatus.addPattern(b, LedPattern.Connected);
  LedStatus.addDisplay(b, display);
  LedStatus.addDraft(b, draft);
  const status = LedStatus.endLedStatus(b);
  const { ended = 0, failed = 0 } = extra(b);
  LedStatusEvent.startLedStatusEvent(b);
  LedStatusEvent.addStatus(b, status);
  if (ended) LedStatusEvent.addEnded(b, ended);
  if (failed) LedStatusEvent.addFailed(b, failed);
  b.finish(LedStatusEvent.endLedStatusEvent(b));
  return LedStatusEvent.getRootAsLedStatusEvent(new ByteBuffer(b.asUint8Array()));
}

describe('mapLedStatus', () => {
  it('maps what shows and who holds each override', () => {
    expect(mapLedStatus(buildEvent(() => ({})).status()!)).toEqual({
      showing: 'color',
      pattern: LedPattern.Connected,
      display: { owner: 2, timeoutMs: 2000 },
      draft: { owner: 1, timeoutMs: 10000 },
    });
  });
});

describe('mapLedNotice', () => {
  it('is null for a status change alone', () => {
    expect(mapLedNotice(buildEvent(() => ({})))).toBeNull();
  });

  it('maps an override ending', () => {
    const event = buildEvent((b) => ({
      ended: LedOverrideEnded.createLedOverrideEnded(
        b,
        LedOverrideLayer.Draft,
        LedOverrideEnd.Expired,
        1,
        42
      ),
    }));
    expect(mapLedNotice(event)).toEqual({
      kind: 'ended',
      layer: LedOverrideLayer.Draft,
      reason: LedOverrideEnd.Expired,
      owner: 1,
      seq: 42,
    });
  });

  it('maps a failed request', () => {
    const event = buildEvent((b) => ({
      failed: LedRequestFailed.createLedRequestFailed(b, 3, 7, LedRequestError.SaveFailed),
    }));
    expect(mapLedNotice(event)).toEqual({
      kind: 'failed',
      error: LedRequestError.SaveFailed,
      owner: 3,
      seq: 7,
    });
  });
});
