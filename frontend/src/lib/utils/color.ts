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
