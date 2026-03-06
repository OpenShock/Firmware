import { describe, expect, it } from 'vitest';
import {
  isArrayBuffer,
  isBoolean,
  isDate,
  isNumber,
  isObject,
  isString,
  isStringOrNull,
} from '$lib/typeguards/basicGuards';

describe('isString', () => {
  it('returns true for string primitives', () => {
    expect(isString('hello')).toBe(true);
    expect(isString('')).toBe(true);
  });

  it('returns true for String objects', () => {
    expect(isString(new String('hello'))).toBe(true);
  });

  it('returns false for non-strings', () => {
    expect(isString(1)).toBe(false);
    expect(isString(null)).toBe(false);
    expect(isString(undefined)).toBe(false);
    expect(isString({})).toBe(false);
    expect(isString([])).toBe(false);
  });
});

describe('isNumber', () => {
  it('returns true for finite numbers', () => {
    expect(isNumber(0)).toBe(true);
    expect(isNumber(42)).toBe(true);
    expect(isNumber(-1.5)).toBe(true);
  });

  it('returns false for NaN and Infinity', () => {
    expect(isNumber(NaN)).toBe(false);
    expect(isNumber(Infinity)).toBe(false);
    expect(isNumber(-Infinity)).toBe(false);
  });

  it('returns false for non-numbers', () => {
    expect(isNumber('42')).toBe(false);
    expect(isNumber(null)).toBe(false);
  });
});

describe('isBoolean', () => {
  it('returns true for booleans', () => {
    expect(isBoolean(true)).toBe(true);
    expect(isBoolean(false)).toBe(true);
  });

  it('returns false for truthy/falsy non-booleans', () => {
    expect(isBoolean(0)).toBe(false);
    expect(isBoolean(1)).toBe(false);
    expect(isBoolean('')).toBe(false);
    expect(isBoolean(null)).toBe(false);
  });
});

describe('isObject', () => {
  it('returns true for plain objects', () => {
    expect(isObject({})).toBe(true);
    expect(isObject({ a: 1 })).toBe(true);
  });

  it('returns false for null', () => {
    expect(isObject(null)).toBe(false);
  });

  it('returns false for arrays', () => {
    expect(isObject([])).toBe(false);
  });

  it('returns false for primitives', () => {
    expect(isObject('string')).toBe(false);
    expect(isObject(42)).toBe(false);
  });
});

describe('isArrayBuffer', () => {
  it('returns true for ArrayBuffer instances', () => {
    expect(isArrayBuffer(new ArrayBuffer(8))).toBe(true);
  });

  it('returns false for typed arrays and other values', () => {
    expect(isArrayBuffer(new Uint8Array(8))).toBe(false);
    expect(isArrayBuffer(null)).toBe(false);
    expect(isArrayBuffer('buffer')).toBe(false);
  });
});

describe('isDate', () => {
  it('returns true for Date instances', () => {
    expect(isDate(new Date())).toBe(true);
  });

  it('returns false for date strings and numbers', () => {
    expect(isDate('2024-01-01')).toBe(false);
    expect(isDate(Date.now())).toBe(false);
  });
});

describe('isStringOrNull', () => {
  it('returns true for strings and null', () => {
    expect(isStringOrNull('hello')).toBe(true);
    expect(isStringOrNull('')).toBe(true);
    expect(isStringOrNull(null)).toBe(true);
  });

  it('returns false for undefined and other types', () => {
    expect(isStringOrNull(undefined)).toBe(false);
    expect(isStringOrNull(0)).toBe(false);
    expect(isStringOrNull(false)).toBe(false);
  });
});
