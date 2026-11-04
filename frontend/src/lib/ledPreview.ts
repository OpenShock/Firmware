import { LedPreviewColorMessage } from '#lib/_fbs/open-shock/serialization/local/led-preview-color-message.js';
import { LedPreviewPatternMessage } from '#lib/_fbs/open-shock/serialization/local/led-preview-pattern-message.js';
import { LedPreviewPattern } from '#lib/_fbs/open-shock/serialization/local/led-preview-pattern.js';
import { LedPreviewStopMessage } from '#lib/_fbs/open-shock/serialization/local/led-preview-stop-message.js';
import { LedTarget } from '#lib/_fbs/open-shock/serialization/local/led-target.js';
import { LocalToHubMessagePayload } from '#lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload.js';
import { LocalToHubMessage } from '#lib/_fbs/open-shock/serialization/local/local-to-hub-message.js';
import { packRgb10, type Rgb } from '#lib/utils/color.js';
import { ConnectionState, WebSocketClient } from '#lib/WebSocketClient.js';
import { Builder as FlatbufferBuilder } from 'flatbuffers';

// LED previews take over the hub's status LEDs for a few seconds after the last message; nothing is saved. They go
// over the WebSocket rather than HTTP: a dragged color wheel or slider sends them many times a second.

export { LedPreviewPattern, LedTarget };

// At most this often while dragging; the LED can't show changes faster than a person can see them anyway
const MIN_INTERVAL_MS = 40;

function send(
  payloadType: LocalToHubMessagePayload,
  build: (fbb: FlatbufferBuilder) => number
): boolean {
  const fbb = new FlatbufferBuilder(64);
  const payload = build(fbb);
  const msg = LocalToHubMessage.createLocalToHubMessage(fbb, payloadType, payload);
  fbb.finish(msg);
  return WebSocketClient.Instance.Send(new Uint8Array(fbb.asUint8Array()));
}

export function previewLedPattern(pattern: LedPreviewPattern): boolean {
  return send(LocalToHubMessagePayload.LedPreviewPatternMessage, (fbb) =>
    LedPreviewPatternMessage.createLedPreviewPatternMessage(fbb, pattern)
  );
}

export function stopLedPreview(): boolean {
  cancelPendingColor();
  return send(LocalToHubMessagePayload.LedPreviewStopMessage, (fbb) => {
    LedPreviewStopMessage.startLedPreviewStopMessage(fbb);
    return LedPreviewStopMessage.endLedPreviewStopMessage(fbb);
  });
}

export interface LedColorPreview {
  color: Rgb;
  target?: LedTarget; // Default: every LED
  brightness?: number; // Percent for this preview only; default: each LED's saved normal brightness
}

function sendColor({ color, target = LedTarget.All, brightness }: LedColorPreview): boolean {
  return send(LocalToHubMessagePayload.LedPreviewColorMessage, (fbb) =>
    LedPreviewColorMessage.createLedPreviewColorMessage(
      fbb,
      packRgb10(color),
      target,
      brightness ?? null
    )
  );
}

let pending: LedColorPreview | null = null;
let timer: ReturnType<typeof setTimeout> | null = null;
let lastSent = 0;

function cancelPendingColor() {
  pending = null;
  if (timer !== null) {
    clearTimeout(timer);
    timer = null;
  }
}

function flush() {
  timer = null;
  if (!pending) return;
  // Still sending the previous frame: wait rather than queue up stale colors
  if (WebSocketClient.Instance.BufferedAmount > 0) {
    timer = setTimeout(flush, MIN_INTERVAL_MS);
    return;
  }
  const next = pending;
  pending = null;
  lastSent = Date.now();
  sendColor(next);
}

/**
 * Previews a color, rate limited: calls faster than every 40 ms collapse into the latest one, which is sent once the
 * interval has passed. Returns false when not connected to the hub.
 */
export function previewLedColor(preview: LedColorPreview): boolean {
  if (WebSocketClient.Instance.ConnectionState !== ConnectionState.CONNECTED) return false;
  pending = preview;
  if (timer === null) {
    const wait = Math.max(0, lastSent + MIN_INTERVAL_MS - Date.now());
    timer = setTimeout(flush, wait);
  }
  return true;
}
