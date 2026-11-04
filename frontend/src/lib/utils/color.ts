/** A color with each channel 0-1. */
export interface Rgb {
  r: number;
  g: number;
  b: number;
}

/** An RRGGBB hex color. */
export function hexToRgb(hex: string): Rgb {
  const value = parseInt(hex, 16);
  return {
    r: ((value >> 16) & 0xff) / 255,
    g: ((value >> 8) & 0xff) / 255,
    b: (value & 0xff) / 255,
  };
}

const RGB10_MAX = 1023;
const toRgb10Channel = (v: number) => Math.min(RGB10_MAX, Math.max(0, Math.round(v * RGB10_MAX)));

/**
 * Packs a color into RGB10: red in bits 20-29, green in 10-19, blue in 0-9, bits 30-31 zero. Channels outside 0-1 are
 * clamped. Matches ColorUtils.h in the firmware.
 */
export function packRgb10({ r, g, b }: Rgb): number {
  // `>>> 0` keeps it an unsigned 32-bit number
  return ((toRgb10Channel(r) << 20) | (toRgb10Channel(g) << 10) | toRgb10Channel(b)) >>> 0;
}

/** Unpacks RGB10 into a color with each channel 0-1. */
export function unpackRgb10(packed: number): Rgb {
  return {
    r: ((packed >>> 20) & RGB10_MAX) / RGB10_MAX,
    g: ((packed >>> 10) & RGB10_MAX) / RGB10_MAX,
    b: (packed & RGB10_MAX) / RGB10_MAX,
  };
}
