export function isObject(value: unknown): value is object {
  return typeof value === 'object' && !Array.isArray(value) && value !== null;
}
export function isString(value: unknown): value is string {
  return typeof value === 'string' || value instanceof String;
}
export function isNumber(value: unknown): value is number {
  return typeof value === 'number' && isFinite(value);
}
export function isBoolean(value: unknown): value is boolean {
  return typeof value === 'boolean';
}
export function isArrayBuffer(value: unknown): value is ArrayBuffer {
  return value instanceof ArrayBuffer;
}
export function isDate(value: unknown): value is Date {
  return value instanceof Date;
}
