export function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && !Array.isArray(value) && value !== null;
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
