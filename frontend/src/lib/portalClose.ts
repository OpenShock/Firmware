import { getApiBaseUrl } from '$lib/utils/localRedirect';

/**
 * Requests the firmware to close the captive portal.
 * The fetch may fail/abort if the portal closes before the response arrives — that's expected.
 */
export async function closePortal(): Promise<void> {
  try {
    await fetch(getApiBaseUrl() + '/api/portal/close', { method: 'POST' });
  } catch {
    // Portal may close before response arrives — ignore network errors
  }
}
