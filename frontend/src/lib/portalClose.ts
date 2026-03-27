import { getApiBaseUrl } from '$lib/utils/localRedirect';

/**
 * Signals the firmware that the user has completed setup.
 * The portal will close gracefully once the device is fully online.
 */
export async function closePortal(): Promise<void> {
  try {
    await fetch(getApiBaseUrl() + '/api/portal/close', { method: 'POST' });
  } catch {
    // Portal may close before response arrives — ignore network errors
  }
}
