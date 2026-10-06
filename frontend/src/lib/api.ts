import { hubState } from '#lib/stores/index.js';
import { getApiBaseUrl } from '#lib/utils/localRedirect.js';
import { toast } from 'svelte-sonner';

function apiFetch(path: string, init?: RequestInit): Promise<Response> {
  return fetch(getApiBaseUrl() + path, init);
}

async function getErrorMessage(res: Response): Promise<string> {
  try {
    const data = await res.json();
    return data.error ?? 'Unknown error';
  } catch {
    return 'Unknown error';
  }
}

// Board info

export async function fetchBoardInfo(): Promise<void> {
  try {
    const res = await apiFetch('/api/board');
    const data = await res.json();
    hubState.hasPredefinedPins = data.has_predefined_pins ?? false;
  } catch {
    // Non-fatal — hasPredefinedPins stays false (DIY flow shown)
  }
}

// WiFi

export async function startWifiScan(): Promise<void> {
  try {
    const res = await apiFetch('/api/wifi/scan?run=1', { method: 'POST' });
    if (!res.ok) {
      toast.error('Failed to start WiFi scan: ' + (await getErrorMessage(res)));
    }
  } catch {
    toast.error('Failed to start WiFi scan');
  }
}

export async function stopWifiScan(): Promise<void> {
  try {
    const res = await apiFetch('/api/wifi/scan?run=0', { method: 'POST' });
    if (!res.ok) {
      toast.error('Failed to stop WiFi scan: ' + (await getErrorMessage(res)));
    }
  } catch {
    toast.error('Failed to stop WiFi scan');
  }
}

export async function forgetWifiNetwork(ssid: string): Promise<void> {
  try {
    const res = await apiFetch('/api/wifi/networks?' + new URLSearchParams({ ssid }), {
      method: 'DELETE',
    });
    if (res.ok) {
      toast.success('Forgot network: ' + ssid);
    } else {
      toast.error('Failed to forget network: ' + (await getErrorMessage(res)));
    }
  } catch {
    toast.error('Failed to forget network');
  }
}

// Account

const _accountLinkErrorMessages: Record<string, string> = {
  CodeRequired: 'Code required',
  InvalidCodeLength: 'Invalid code length',
  NoInternetConnection: 'No internet connection',
  InvalidCode: 'Invalid code',
  RateLimited: 'Too many requests',
  RequestFailed: 'Could not reach the server (check DNS/TLS/connection)',
  RequestTimedOut: 'Request to the server timed out',
  ServerError: 'Server returned an unexpected response',
  InvalidResponse: 'Server sent an invalid response',
  ConfigSaveFailed: 'Failed to save the auth token to the device',
  InternalError: 'Internal error',
};

export async function linkAccount(code: string): Promise<boolean> {
  try {
    const res = await apiFetch('/api/account/link?' + new URLSearchParams({ code }), {
      method: 'POST',
    });
    if (res.ok) {
      hubState.accountLinked = true;
      toast.success('Account linked successfully');
      return true;
    } else {
      const error = await getErrorMessage(res);
      const reason = _accountLinkErrorMessages[error] ?? 'Unknown error';
      toast.error('Failed to link account: ' + reason);
      return false;
    }
  } catch {
    toast.error('Failed to link account');
    return false;
  }
}

export async function unlinkAccount(): Promise<void> {
  try {
    const res = await apiFetch('/api/account', { method: 'DELETE' });
    if (res.ok) {
      hubState.accountLinked = false;
      toast.success('Account unlinked');
    } else {
      toast.error('Failed to unlink account: ' + (await getErrorMessage(res)));
    }
  } catch {
    toast.error('Failed to unlink account');
  }
}

// Config - RF

const _gpioErrorMessages: Record<string, string> = {
  InvalidPin: 'Invalid pin',
  InternalError: 'Internal error',
};

export async function setRfTxPin(pin: number): Promise<boolean> {
  try {
    const res = await apiFetch('/api/config/rf/pin?' + new URLSearchParams({ pin: String(pin) }), {
      method: 'PUT',
    });
    if (res.ok) {
      const data = await res.json();
      if (hubState.config) hubState.config.rf.txPin = data.pin;
      toast.success('Changed RF TX pin to: ' + data.pin);
      return true;
    } else {
      const error = await getErrorMessage(res);
      toast.error('Failed to change RF TX pin: ' + (_gpioErrorMessages[error] ?? 'Unknown error'));
      return false;
    }
  } catch {
    toast.error('Failed to change RF TX pin');
    return false;
  }
}

// Config - EStop

export async function setEstopPin(pin: number): Promise<boolean> {
  try {
    const res = await apiFetch(
      '/api/config/estop/pin?' + new URLSearchParams({ pin: String(pin) }),
      {
        method: 'PUT',
      }
    );
    if (res.ok) {
      const data = await res.json();
      if (hubState.config) hubState.config.estop.gpioPin = data.pin;
      toast.success('Changed EStop pin to: ' + data.pin);
      return true;
    } else {
      const error = await getErrorMessage(res);
      toast.error('Failed to change EStop pin: ' + (_gpioErrorMessages[error] ?? 'Unknown error'));
      return false;
    }
  } catch {
    toast.error('Failed to change EStop pin');
    return false;
  }
}

export async function setEstopEnabled(enabled: boolean): Promise<boolean> {
  try {
    const res = await apiFetch(
      '/api/config/estop/enabled?' + new URLSearchParams({ enabled: enabled ? '1' : '0' }),
      { method: 'PUT' }
    );
    if (res.ok) {
      if (hubState.config) hubState.config.estop.enabled = enabled;
      toast.success('Changed EStop enabled to: ' + enabled);
      return true;
    } else {
      toast.error('Failed to change EStop enabled');
      return false;
    }
  } catch {
    toast.error('Failed to change EStop enabled');
    return false;
  }
}

// WiFi network management

export async function saveWifiNetwork(
  ssid: string,
  password: string | null,
  connect: boolean,
  security?: number
): Promise<boolean> {
  try {
    const params = new URLSearchParams({ ssid, connect: connect ? '1' : '0' });
    if (password) params.set('password', password);
    if (security !== undefined) params.set('security', String(security));
    const res = await apiFetch('/api/wifi/networks', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: params.toString(),
    });
    if (!res.ok) {
      toast.error('Failed to save WiFi network: ' + (await getErrorMessage(res)));
      return false;
    }
    return true;
  } catch {
    toast.error('Failed to save WiFi network');
    return false;
  }
}

export async function connectWifiNetwork(ssid: string): Promise<void> {
  try {
    const res = await apiFetch('/api/wifi/connect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ ssid }).toString(),
    });
    if (!res.ok) {
      toast.error('Failed to connect to WiFi network: ' + (await getErrorMessage(res)));
    }
  } catch {
    toast.error('Failed to connect to WiFi network');
  }
}

export async function disconnectWifiNetwork(): Promise<void> {
  try {
    const res = await apiFetch('/api/wifi/disconnect', { method: 'POST' });
    if (!res.ok) {
      toast.error('Failed to disconnect from WiFi network');
    }
  } catch {
    toast.error('Failed to disconnect from WiFi network');
  }
}

// OTA

async function otaRequest(path: string, method: 'PUT' | 'POST', failure: string): Promise<void> {
  try {
    const res = await apiFetch('/api/ota/' + path, { method });
    if (!res.ok) {
      toast.error(failure + ': ' + (await getErrorMessage(res)));
    }
  } catch {
    toast.error(failure);
  }
}

export function setOtaEnabled(enabled: boolean): Promise<void> {
  return otaRequest(
    `enabled?enabled=${enabled ? '1' : '0'}`,
    'PUT',
    'Failed to update OTA setting'
  );
}

export function setOtaDomain(domain: string): Promise<void> {
  return otaRequest(
    'domain?' + new URLSearchParams({ domain }),
    'PUT',
    'Failed to update OTA domain'
  );
}

export function setOtaChannel(channel: string): Promise<void> {
  return otaRequest(
    'channel?' + new URLSearchParams({ channel }),
    'PUT',
    'Failed to update OTA channel'
  );
}

export function setOtaCheckInterval(interval: number): Promise<void> {
  return otaRequest(
    `check-interval?interval=${interval}`,
    'PUT',
    'Failed to update OTA check interval'
  );
}

export function setOtaAllowBackendManagement(allow: boolean): Promise<void> {
  return otaRequest(
    `allow-backend-management?allow=${allow ? '1' : '0'}`,
    'PUT',
    'Failed to update OTA backend management setting'
  );
}

export function setOtaRequireManualApproval(require: boolean): Promise<void> {
  return otaRequest(
    `require-manual-approval?require=${require ? '1' : '0'}`,
    'PUT',
    'Failed to update OTA manual approval setting'
  );
}

export function checkOtaUpdates(channel: string): Promise<void> {
  return otaRequest(
    'check?' + new URLSearchParams({ channel }),
    'POST',
    'Failed to check for OTA updates'
  );
}
