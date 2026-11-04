import { describe, expect, it } from 'vitest';
import { hexToRgb, packRgb10, unpackRgb10 } from './color';

describe('packRgb10', () => {
  it('packs the channels into their bit ranges', () => {
    expect(packRgb10({ r: 1, g: 0, b: 0 })).toBe(0x3ff00000);
    expect(packRgb10({ r: 0, g: 1, b: 0 })).toBe(0x000ffc00);
    expect(packRgb10({ r: 0, g: 0, b: 1 })).toBe(0x000003ff);
  });

  it('keeps full white unsigned with the top bits clear', () => {
    expect(packRgb10({ r: 1, g: 1, b: 1 })).toBe(0x3fffffff);
  });

  it('clamps channels outside 0-1', () => {
    expect(packRgb10({ r: 2, g: -1, b: 0.5 })).toBe(packRgb10({ r: 1, g: 0, b: 0.5 }));
  });

  it('round-trips to within one 10-bit step', () => {
    const color = { r: 0.8104, g: 0.52, b: 0.1251 };
    const back = unpackRgb10(packRgb10(color));
    for (const k of ['r', 'g', 'b'] as const)
      expect(Math.abs(back[k] - color[k])).toBeLessThan(1 / 1023);
  });
});

describe('hexToRgb', () => {
  it('maps ff to 1 and 00 to 0', () => {
    expect(hexToRgb('ff0080')).toEqual({ r: 1, g: 0, b: 128 / 255 });
  });
});
