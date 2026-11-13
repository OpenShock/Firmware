import { describe, expect, it } from 'vitest';
import { hexToRgb } from './color';

describe('hexToRgb', () => {
  it('maps ff to 1 and 00 to 0', () => {
    expect(hexToRgb('ff0080')).toEqual({ r: 1, g: 0, b: 128 / 255 });
  });
});
