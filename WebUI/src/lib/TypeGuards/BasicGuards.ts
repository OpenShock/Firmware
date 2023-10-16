export function isObject(data: unknown): data is object {
  return typeof data === 'object' && data !== null;
}
export function isString(value: unknown): value is string {
  return typeof value === 'string' || value instanceof String;
}
export function isNumber(value: unknown): value is number {
  return typeof value === 'number' && isFinite(value);
}
export function isArrayBuffer(value: unknown): value is ArrayBuffer {
  return value instanceof ArrayBuffer;
}
