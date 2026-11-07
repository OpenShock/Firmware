import { OtaChannel } from '#lib/_fbs/open-shock/serialization/local/ota-channel.js';
import type { AccessPointConfig, OtaUpdateConfig } from '#lib/mappers/ConfigMapper.js';
import { hubState } from '#lib/stores/index.js';
import { getApiBaseUrl } from '#lib/utils/localRedirect.js';
import { toast } from 'svelte-sonner';

function apiFetch(path: string, init?: RequestInit): Promise<Response> {
  return fetch(getApiBaseUrl() + path, init);
}

// Error codes returned by the hub's HTTP API, mapped to user-facing text. Unknown codes are shown as-is.
const _errorMessages: Record<string, string> = {
  // Account linking
  CodeRequired: 'Code required',
  InvalidCodeLength: 'Invalid code length',
  NoInternetConnection: 'No internet connection',
  InvalidCode: 'Invalid code',
  RequestFailed: 'Could not reach the server (check DNS/TLS/connection)',
  RequestTimedOut: 'Request to the server timed out',
  ServerError: 'Server returned an unexpected response',
  InvalidResponse: 'Server sent an invalid response',
  ConfigSaveFailed: 'Failed to save the auth token to the device',
  // GPIO
  InvalidPin: 'Invalid pin',
  PinInUse: 'Pin is used by the E-Stop',
  // WiFi
  MissingSsid: 'Network name is required',
  InvalidSsid: 'Network name must be 1-32 bytes',
  PasswordTooShort: 'Password must be at least 8 characters',
  PasswordTooLong: 'Password must be at most 63 characters',
  // OTA
  InvalidChannel: 'Invalid update channel',
  // Access point
  InvalidApSsid: "Network name must be 1-32 bytes, without '|' or control characters",
  InvalidApPassword: 'Password must be printable ASCII characters (no accents or emoji)',
  // Generic
  MissingParam: 'Missing parameter',
  InvalidParam: 'Invalid value',
  RateLimited: 'Too many requests',
  InternalError: 'Internal error',
};

async function getErrorMessage(res: Response): Promise<string> {
  try {
    const data = await res.json();
    const code: unknown = data?.error;
    if (typeof code !== 'string') return 'Unknown error';
    return _errorMessages[code] ?? code;
  } catch {
    return 'Unknown error';
  }
}

// Board info

export async function fetchBoardInfo(): Promise<void> {
  try {
    const res = await apiFetch('/api/board');
    if (!res.ok) return; // Non-fatal, see below
    const data = await res.json();
    hubState.hasPredefinedPins = data.has_predefined_pins ?? false;
  } catch {
    // Non-fatal: hasPredefinedPins stays false (DIY flow shown)
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
    // Success is announced by the hub's "Removed" WiFi event
    if (!res.ok) {
      toast.error('Failed to forget network: ' + (await getErrorMessage(res)));
    }
  } catch {
    toast.error('Failed to forget network');
  }
}

// Account

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
      toast.error('Failed to link account: ' + (await getErrorMessage(res)));
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
      toast.error('Failed to change RF TX pin: ' + (await getErrorMessage(res)));
      return false;
    }
  } catch {
    toast.error('Failed to change RF TX pin');
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

const _otaChannelNames: Record<OtaChannel, string> = {
  [OtaChannel.Stable]: 'stable',
  [OtaChannel.Beta]: 'beta',
  [OtaChannel.Develop]: 'develop',
};

// On success the change is mirrored into hubState.config, which is otherwise only sent once on
// connect, so the controls reflect what the hub stored.
async function otaRequest(
  path: string,
  method: 'PUT' | 'POST',
  failure: string,
  patch?: Partial<OtaUpdateConfig>
): Promise<boolean> {
  try {
    const res = await apiFetch('/api/ota/' + path, { method });
    if (!res.ok) {
      toast.error(failure + ': ' + (await getErrorMessage(res)));
      return false;
    }
    if (patch && hubState.config) Object.assign(hubState.config.otaUpdate, patch);
    return true;
  } catch {
    toast.error(failure);
    return false;
  }
}

// All OTA settings go through PUT /api/ota/settings, which applies any subset in one config write.
function setOtaSettings(
  params: Record<string, string>,
  failure: string,
  patch: Partial<OtaUpdateConfig>
): Promise<boolean> {
  return otaRequest('settings?' + new URLSearchParams(params), 'PUT', failure, patch);
}

export function setOtaEnabled(isEnabled: boolean): Promise<boolean> {
  return setOtaSettings({ enabled: isEnabled ? '1' : '0' }, 'Failed to update OTA setting', {
    isEnabled,
  });
}

export function setOtaChannel(updateChannel: OtaChannel): Promise<boolean> {
  return setOtaSettings(
    { channel: _otaChannelNames[updateChannel] },
    'Failed to update OTA channel',
    { updateChannel }
  );
}

export function setOtaCheckInterval(checkInterval: number): Promise<boolean> {
  return setOtaSettings(
    { interval: String(checkInterval) },
    'Failed to update OTA check interval',
    { checkInterval }
  );
}

export function setOtaAllowBackendManagement(allowBackendManagement: boolean): Promise<boolean> {
  return setOtaSettings(
    { allow: allowBackendManagement ? '1' : '0' },
    'Failed to update OTA backend management setting',
    {
      allowBackendManagement,
    }
  );
}

export function setOtaRequireManualApproval(requireManualApproval: boolean): Promise<boolean> {
  return setOtaSettings(
    { require: requireManualApproval ? '1' : '0' },
    'Failed to update OTA manual approval setting',
    {
      requireManualApproval,
    }
  );
}

export function checkOtaUpdates(channel: OtaChannel): Promise<boolean> {
  return otaRequest(
    'check?' + new URLSearchParams({ channel: _otaChannelNames[channel] }),
    'POST',
    'Failed to check for OTA updates'
  );
}

export interface AccessPointChange {
  ssid?: string; // '' for the default name
  protection?: AccessPointConfig['protection'];
  password?: string; // With protection 'custom'
}

const _protectionKinds: readonly AccessPointConfig['protection'][] = ['default', 'open', 'custom'];

// The captive portal's access point (PUT /api/ap). Saved at once, applied the next time the portal starts. The hub
// answers with the settings as it reads them (a name equal to the bare prefix is the default, and so is the build's
// default password), which replace them in hubState.config, otherwise only sent once on connect.
export async function setAccessPoint(change: AccessPointChange): Promise<boolean> {
  const params = new URLSearchParams();
  if (change.ssid !== undefined) params.set('ssid', change.ssid);
  if (change.protection !== undefined) params.set('protection', change.protection);
  if (change.protection === 'custom' && change.password !== undefined)
    params.set('password', change.password);
  try {
    const res = await apiFetch('/api/ap', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: params.toString(),
    });
    if (!res.ok) {
      toast.error('Failed to save the access point: ' + (await getErrorMessage(res)));
      return false;
    }
    const ap = hubState.config?.accessPoint;
    if (ap) {
      const data = await res.json();
      if (typeof data.ssid === 'string') ap.ssid = data.ssid || null;
      if (_protectionKinds.includes(data.protection)) ap.protection = data.protection;
    }
    return true;
  } catch {
    toast.error('Failed to save the access point');
    return false;
  }
}
