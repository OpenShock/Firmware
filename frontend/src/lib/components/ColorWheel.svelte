<script lang="ts">
  // Hue around the ring, saturation from the center out; value is always full (brightness is set separately).
  import type { Rgb } from '#lib/utils/color.js';

  interface Props {
    color?: string; // RRGGBB, for showing the pick
    size?: number;
    disabled?: boolean;
    /** The pick, each channel 0-1, straight from the hue and saturation without rounding to 8 bits first. */
    onchange?: (color: Rgb) => void;
  }

  let { color = $bindable('ff0000'), size = 200, disabled = false, onchange }: Props = $props();

  // White in the middle fading out over the hue ring
  const WHEEL_BACKGROUND =
    'radial-gradient(circle closest-side, #fff, transparent), conic-gradient(red, yellow, lime, cyan, blue, magenta, red)';

  let wheel = $state<HTMLDivElement | null>(null);
  let hue = $state(0); // degrees, 0 = red at the top, clockwise
  let saturation = $state(1);

  // Each channel 0-1 for a hue (degrees) and saturation, at full value
  function hsvChannels(h: number, s: number): [number, number, number] {
    const f = (n: number) => {
      const k = (n + h / 60) % 6;
      return 1 - s * Math.max(0, Math.min(k, 4 - k, 1));
    };
    return [f(5), f(3), f(1)];
  }

  function emit() {
    const channels = hsvChannels(hue, saturation);
    color = channels
      .map((v) =>
        Math.round(v * 255)
          .toString(16)
          .padStart(2, '0')
      )
      .join('');
    const [r, g, b] = channels;
    onchange?.({ r, g, b });
  }

  function pick(e: PointerEvent) {
    if (!wheel || disabled) return;
    const rect = wheel.getBoundingClientRect();
    const x = e.clientX - rect.left - rect.width / 2;
    const y = e.clientY - rect.top - rect.height / 2;
    hue = ((Math.atan2(x, -y) * 180) / Math.PI + 360) % 360;
    saturation = Math.min(1, Math.hypot(x, y) / (rect.width / 2));
    emit();
  }

  function onpointerdown(e: PointerEvent) {
    if (disabled) return;
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
    pick(e);
  }

  function onpointermove(e: PointerEvent) {
    if ((e.currentTarget as HTMLElement).hasPointerCapture(e.pointerId)) pick(e);
  }

  // Keyboard: arrows turn the hue, shift+arrows change the saturation
  function onkeydown(e: KeyboardEvent) {
    if (disabled) return;
    const step = e.shiftKey ? 0.1 : 10;
    if (e.key === 'ArrowRight' || e.key === 'ArrowUp') {
      if (e.shiftKey) saturation = Math.min(1, saturation + step);
      else hue = (hue + step) % 360;
    } else if (e.key === 'ArrowLeft' || e.key === 'ArrowDown') {
      if (e.shiftKey) saturation = Math.max(0, saturation - step);
      else hue = (hue - step + 360) % 360;
    } else {
      return;
    }
    e.preventDefault();
    emit();
  }

  let markerX = $derived(50 + Math.sin((hue * Math.PI) / 180) * saturation * 50);
  let markerY = $derived(50 - Math.cos((hue * Math.PI) / 180) * saturation * 50);
</script>

<div
  bind:this={wheel}
  class={[
    'relative shrink-0 touch-none rounded-full select-none',
    disabled ? 'cursor-not-allowed opacity-50' : 'cursor-crosshair',
  ]}
  style:width="{size}px"
  style:height="{size}px"
  style:background={WHEEL_BACKGROUND}
  role="slider"
  tabindex={disabled ? -1 : 0}
  aria-label="Color"
  aria-valuemin={0}
  aria-valuemax={359}
  aria-valuenow={Math.round(hue)}
  aria-valuetext={'#' + color}
  aria-disabled={disabled}
  {onpointerdown}
  {onpointermove}
  {onkeydown}
>
  <span
    class="pointer-events-none absolute size-4 -translate-x-1/2 -translate-y-1/2 rounded-full border-2 border-white shadow ring-1 ring-black/40"
    style:left="{markerX}%"
    style:top="{markerY}%"
    style:background={'#' + color}
  ></span>
</div>
