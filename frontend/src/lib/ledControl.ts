import { LedCommitDraftMessage } from '#lib/_fbs/open-shock/serialization/local/led-commit-draft-message.js';
import { LedDraftMessage } from '#lib/_fbs/open-shock/serialization/local/led-draft-message.js';
import { LedOverrideStopMessage } from '#lib/_fbs/open-shock/serialization/local/led-override-stop-message.js';
import { LedPreviewColorMessage } from '#lib/_fbs/open-shock/serialization/local/led-preview-color-message.js';
import { LedPreviewPatternMessage } from '#lib/_fbs/open-shock/serialization/local/led-preview-pattern-message.js';
import { LedSetModeMessage } from '#lib/_fbs/open-shock/serialization/local/led-set-mode-message.js';
import { LedSignal } from '#lib/_fbs/open-shock/serialization/local/led-signal.js';
import { LedTarget } from '#lib/_fbs/open-shock/serialization/local/led-target.js';
import { LocalToHubMessagePayload } from '#lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload.js';
import { LocalToHubMessage } from '#lib/_fbs/open-shock/serialization/local/local-to-hub-message.js';
import { LedBrightness } from '#lib/_fbs/open-shock/serialization/types/led-brightness.js';
import { LedMode as FbsLedMode } from '#lib/_fbs/open-shock/serialization/types/led-mode.js';
import { LedOverrideLayer } from '#lib/_fbs/open-shock/serialization/types/led-override-layer.js';
import { LedPattern } from '#lib/_fbs/open-shock/serialization/types/led-pattern.js';
import type { LedMode } from '#lib/mappers/ConfigMapper.js';
import type { LedNotice } from '#lib/mappers/LedStatusMapper.js';
import type { Rgb } from '#lib/utils/color.js';
import { ConnectionState, WebSocketClient } from '#lib/WebSocketClient.js';
import { Builder as FlatbufferBuilder } from 'flatbuffers';

// Everything the portal does to the status LEDs goes over the WebSocket: previews and unsaved brightness (a dragged
// control sends them many times a second), and saving. The hub times every override and reports each outcome in a
// LedStatusEvent, which the message handler turns into hubState.ledStatus and the notices below. Each send returns
// the request's seq, which the hub echoes in a notice about it, or null when not connected.

export { LedOverrideLayer, LedPattern, LedSignal, LedTarget };

// At most this often while dragging; the LED can't show changes faster than a person can see them anyway
const MIN_INTERVAL_MS = 40;

let nextSeq = 1;
function takeSeq(): number {
  const seq = nextSeq;
  nextSeq = nextSeq >= 0xffff ? 1 : nextSeq + 1;
  return seq;
}

function connected(): boolean {
  return WebSocketClient.Instance.ConnectionState === ConnectionState.CONNECTED;
}

function send(
  payloadType: LocalToHubMessagePayload,
  build: (fbb: FlatbufferBuilder, seq: number) => number
): number | null {
  const seq = takeSeq();
  const fbb = new FlatbufferBuilder(64);
  const payload = build(fbb, seq);
  const msg = LocalToHubMessage.createLocalToHubMessage(fbb, payloadType, payload);
  fbb.finish(msg);
  return WebSocketClient.Instance.Send(new Uint8Array(fbb.asUint8Array())) ? seq : null;
}

// --- Notices from the hub ---

type NoticeListener = (notice: LedNotice) => void;
const listeners = new Set<NoticeListener>();

/** Calls `listener` with every LED notice from the hub, every client's. Returns the unsubscribe function. */
export function onLedNotice(listener: NoticeListener): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

/** For the message handler. */
export function dispatchLedNotice(notice: LedNotice) {
  for (const listener of listeners) listener(notice);
}

// --- Rate-limited streams: the latest color and draft, sent together at most every MIN_INTERVAL_MS ---

export interface LedColorPreview {
  color: Rgb;
  target?: LedTarget; // Default: every LED
  signal?: LedSignal; // The brightness setting it shows at; default: normal
}

export interface LedBrightnessPair {
  critical: number; // 0-65535
  normal: number;
}

export interface LedDraft {
  mono?: LedBrightnessPair;
  rgb?: LedBrightnessPair;
}

// A 0-1 channel as the 16 bits the hub takes
const toUint16 = (v: number) => Math.min(65535, Math.max(0, Math.round(v * 65535)));

function sendColor({ color, target = LedTarget.All, signal = LedSignal.Normal }: LedColorPreview) {
  return send(LocalToHubMessagePayload.LedPreviewColorMessage, (fbb, seq) =>
    LedPreviewColorMessage.createLedPreviewColorMessage(
      fbb,
      seq,
      toUint16(color.r),
      toUint16(color.g),
      toUint16(color.b),
      target,
      signal
    )
  );
}

function sendDraft(draft: LedDraft) {
  return send(LocalToHubMessagePayload.LedDraftMessage, (fbb, seq) => {
    const pair = (p?: LedBrightnessPair) =>
      p ? LedBrightness.createLedBrightness(fbb, p.critical, p.normal) : 0;
    const mono = pair(draft.mono);
    const rgb = pair(draft.rgb);
    LedDraftMessage.startLedDraftMessage(fbb);
    LedDraftMessage.addSeq(fbb, seq);
    if (mono) LedDraftMessage.addMono(fbb, mono);
    if (rgb) LedDraftMessage.addRgb(fbb, rgb);
    return LedDraftMessage.endLedDraftMessage(fbb);
  });
}

let pendingColor: LedColorPreview | null = null;
let pendingDraft: LedDraft | null = null;
let timer: ReturnType<typeof setTimeout> | null = null;
let lastSent = 0;

function schedule() {
  if (timer !== null) return;
  const wait = Math.max(0, lastSent + MIN_INTERVAL_MS - Date.now());
  timer = setTimeout(flush, wait);
}

function flush() {
  timer = null;
  if (!pendingColor && !pendingDraft) return;
  // Still sending the previous frame: wait rather than queue up stale values
  if (WebSocketClient.Instance.BufferedAmount > 0) {
    timer = setTimeout(flush, MIN_INTERVAL_MS);
    return;
  }
  flushNow();
}

// The draft first, so the color it comes with already shows at the new brightness
function flushNow() {
  if (timer !== null) {
    clearTimeout(timer);
    timer = null;
  }
  const draft = pendingDraft;
  const color = pendingColor;
  pendingDraft = null;
  pendingColor = null;
  lastSent = Date.now();
  if (draft) sendDraft(draft);
  if (color) sendColor(color);
}

/** Previews a color, rate limited: calls faster than every 40 ms collapse into the latest one. */
export function previewLedColor(preview: LedColorPreview): boolean {
  if (!connected()) return false;
  pendingColor = preview;
  schedule();
  return true;
}

/** Shows unsaved brightness on the LEDs, rate limited like previewLedColor. */
export function setLedDraft(draft: LedDraft): boolean {
  if (!connected()) return false;
  pendingDraft = draft;
  schedule();
  return true;
}

// --- One-off requests ---

export function previewLedPattern(pattern: LedPattern): number | null {
  pendingColor = null; // A queued color would flush after the pattern and replace it
  return send(LocalToHubMessagePayload.LedPreviewPatternMessage, (fbb, seq) =>
    LedPreviewPatternMessage.createLedPreviewPatternMessage(fbb, seq, pattern)
  );
}

export function stopLedOverride(layer: LedOverrideLayer): number | null {
  if (layer === LedOverrideLayer.Display) pendingColor = null;
  else pendingDraft = null;
  return send(LocalToHubMessagePayload.LedOverrideStopMessage, (fbb, seq) =>
    LedOverrideStopMessage.createLedOverrideStopMessage(fbb, seq, layer)
  );
}

/** Saves the draft the hub holds; a draft still waiting on the rate limit goes out first. */
export function commitLedDraft(): number | null {
  if (!connected()) return null;
  flushNow();
  return send(LocalToHubMessagePayload.LedCommitDraftMessage, (fbb, seq) =>
    LedCommitDraftMessage.createLedCommitDraftMessage(fbb, seq)
  );
}

const _modes: Record<LedMode, FbsLedMode> = {
  all: FbsLedMode.All,
  critical: FbsLedMode.CriticalOnly,
  off: FbsLedMode.Off,
};

export function setLedMode(mode: LedMode): number | null {
  return send(LocalToHubMessagePayload.LedSetModeMessage, (fbb, seq) =>
    LedSetModeMessage.createLedSetModeMessage(fbb, seq, _modes[mode])
  );
}
