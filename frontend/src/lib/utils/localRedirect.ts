const _localHostnames = ['localhost', '127.0.0.1'];

const _localDeviceHost = import.meta.env.VITE_LOCAL_DEVICE_HOST ?? '4.3.2.1';

/**
 * When the frontend is served locally (e.g. via `pnpm dev`), the device is
 * still reachable at the configured local device host (default: 4.3.2.1).
 * Returns that address for local hostnames so both HTTP and WebSocket requests
 * reach the real device.
 */
export function getDeviceHostname(): string {
  const hostname = window.location.hostname;
  return _localHostnames.includes(hostname) ? _localDeviceHost : hostname;
}

/**
 * Returns an absolute base URL (e.g. `http://4.3.2.1`) when running locally,
 * or an empty string when running directly on the device (relative URLs work).
 */
export function getApiBaseUrl(): string {
  const hostname = window.location.hostname;
  return _localHostnames.includes(hostname) ? `http://${_localDeviceHost}` : '';
}
