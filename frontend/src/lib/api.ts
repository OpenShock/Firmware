import { toast } from 'svelte-sonner';
import { hubState } from '$lib/stores';
import { getApiBaseUrl } from '$lib/utils/localRedirect';

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
    const res = await apiFetch('/api/config/estop/pin?' + new URLSearchParams({ pin: String(pin) }), {
      method: 'PUT',
    });
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
      { method: 'PUT' },
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
  security?: number,
): Promise<void> {
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
    }
  } catch {
    toast.error('Failed to save WiFi network');
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

export async function setOtaEnabled(enabled: boolean): Promise<void> {
  try {
    await apiFetch(`/api/ota/enabled?enabled=${enabled ? '1' : '0'}`, { method: 'PUT' });
  } catch {
    toast.error('Failed to update OTA setting');
  }
}

export async function setOtaDomain(domain: string): Promise<void> {
  try {
    await apiFetch(`/api/ota/domain?` + new URLSearchParams({ domain }), { method: 'PUT' });
  } catch {
    toast.error('Failed to update OTA domain');
  }
}

export async function setOtaChannel(channel: string): Promise<void> {
  try {
    await apiFetch(`/api/ota/channel?` + new URLSearchParams({ channel }), { method: 'PUT' });
  } catch {
    toast.error('Failed to update OTA channel');
  }
}

export async function setOtaCheckInterval(interval: number): Promise<void> {
  try {
    await apiFetch(`/api/ota/check-interval?interval=${interval}`, { method: 'PUT' });
  } catch {
    toast.error('Failed to update OTA check interval');
  }
}

export async function setOtaAllowBackendManagement(allow: boolean): Promise<void> {
  try {
    await apiFetch(`/api/ota/allow-backend-management?allow=${allow ? '1' : '0'}`, { method: 'PUT' });
  } catch {
    toast.error('Failed to update OTA backend management setting');
  }
}

export async function setOtaRequireManualApproval(require: boolean): Promise<void> {
  try {
    await apiFetch(`/api/ota/require-manual-approval?require=${require ? '1' : '0'}`, {
      method: 'PUT',
    });
  } catch {
    toast.error('Failed to update OTA manual approval setting');
  }
}

export async function checkOtaUpdates(channel: string): Promise<void> {
  try {
    await apiFetch(`/api/ota/check?` + new URLSearchParams({ channel }), { method: 'POST' });
  } catch {
    toast.error('Failed to check for OTA updates');
  }
}
