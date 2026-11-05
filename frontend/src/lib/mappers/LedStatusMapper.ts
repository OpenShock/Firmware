import type { LedOverrideEnd } from '#lib/_fbs/open-shock/serialization/local/led-override-end.js';
import type { LedOverride } from '#lib/_fbs/open-shock/serialization/local/led-override.js';
import type { LedRequestError } from '#lib/_fbs/open-shock/serialization/local/led-request-error.js';
import { LedShowing as FbsLedShowing } from '#lib/_fbs/open-shock/serialization/local/led-showing.js';
import type { LedStatusEvent } from '#lib/_fbs/open-shock/serialization/local/led-status-event.js';
import type { LedStatus as FbsLedStatus } from '#lib/_fbs/open-shock/serialization/local/led-status.js';
import type { LedOverrideLayer } from '#lib/_fbs/open-shock/serialization/types/led-override-layer.js';
import type { LedPattern } from '#lib/_fbs/open-shock/serialization/types/led-pattern.js';

// What the hub's status LEDs show, as its LedStatus reports it

export type LedShowing = 'live' | 'hidden' | 'pattern' | 'color' | 'test';

export interface LedOverrideInfo {
  owner: number; // The client holding it (hubState.clientId for this one)
  timeoutMs: number; // How long it lasts after the owner's last message for it
}

export interface LedStatus {
  showing: LedShowing;
  pattern: LedPattern; // For 'live', 'hidden' (the hidden state) and 'pattern'
  display: LedOverrideInfo | null;
  draft: LedOverrideInfo | null;
}

export type LedNotice =
  | { kind: 'ended'; layer: LedOverrideLayer; reason: LedOverrideEnd; owner: number; seq: number }
  | { kind: 'failed'; error: LedRequestError; owner: number; seq: number };

const _showing: Record<FbsLedShowing, LedShowing> = {
  [FbsLedShowing.Live]: 'live',
  [FbsLedShowing.Hidden]: 'hidden',
  [FbsLedShowing.Pattern]: 'pattern',
  [FbsLedShowing.Color]: 'color',
  [FbsLedShowing.Test]: 'test',
};

function mapOverride(o: LedOverride | null): LedOverrideInfo | null {
  return o ? { owner: o.owner(), timeoutMs: o.timeoutMs() } : null;
}

export function mapLedStatus(status: FbsLedStatus): LedStatus {
  return {
    showing: _showing[status.showing()] ?? 'live',
    pattern: status.pattern(),
    display: mapOverride(status.display()),
    draft: mapOverride(status.draft()),
  };
}

export function mapLedNotice(event: LedStatusEvent): LedNotice | null {
  const ended = event.ended();
  if (ended) {
    return {
      kind: 'ended',
      layer: ended.layer(),
      reason: ended.reason(),
      owner: ended.owner(),
      seq: ended.seq(),
    };
  }
  const failed = event.failed();
  if (failed) {
    return { kind: 'failed', error: failed.error(), owner: failed.owner(), seq: failed.seq() };
  }
  return null;
}
