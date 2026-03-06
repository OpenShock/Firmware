import { toast } from 'svelte-sonner';
import { hubState } from '$lib/stores';

// WiFi

export async function startWifiScan(): Promise<void> {
  try {
    const res = await fetch('/api/wifi/scan?run=1', { method: 'POST' });
    const data = await res.json();
    if (!data.ok) {
      toast.error('Failed to start WiFi scan: ' + (data.error ?? 'Unknown error'));
    }
  } catch {
    toast.error('Failed to start WiFi scan');
  }
}

export async function stopWifiScan(): Promise<void> {
  try {
    const res = await fetch('/api/wifi/scan?run=0', { method: 'POST' });
    const data = await res.json();
    if (!data.ok) {
      toast.error('Failed to stop WiFi scan: ' + (data.error ?? 'Unknown error'));
    }
  } catch {
    toast.error('Failed to stop WiFi scan');
  }
}

export async function forgetWifiNetwork(ssid: string): Promise<void> {
  try {
    const res = await fetch('/api/wifi/networks?' + new URLSearchParams({ ssid }), {
      method: 'DELETE',
    });
    const data = await res.json();
    if (data.ok) {
      toast.success('Forgot network: ' + ssid);
    } else {
      toast.error('Failed to forget network: ' + (data.error ?? 'Unknown error'));
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
    const res = await fetch('/api/account/link?' + new URLSearchParams({ code }), {
      method: 'POST',
    });
    const data = await res.json();
    if (data.ok) {
      hubState.accountLinked = true;
      toast.success('Account linked successfully');
      return true;
    } else {
      const reason = _accountLinkErrorMessages[data.error] ?? 'Unknown error';
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
    const res = await fetch('/api/account', { method: 'DELETE' });
    const data = await res.json();
    if (data.ok) {
      hubState.accountLinked = false;
      toast.success('Account unlinked');
    } else {
      toast.error('Failed to unlink account: ' + (data.error ?? 'Unknown error'));
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
    const res = await fetch('/api/config/rf/pin?' + new URLSearchParams({ pin: String(pin) }), {
      method: 'PUT',
    });
    const data = await res.json();
    if (data.ok) {
      if (hubState.config) hubState.config.rf.txPin = pin;
      toast.success('Changed RF TX pin to: ' + pin);
      return true;
    } else {
      toast.error(
        'Failed to change RF TX pin: ' + (_gpioErrorMessages[data.error] ?? 'Unknown error'),
      );
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
    const res = await fetch('/api/config/estop/pin?' + new URLSearchParams({ pin: String(pin) }), {
      method: 'PUT',
    });
    const data = await res.json();
    if (data.ok) {
      if (hubState.config) hubState.config.estop.gpioPin = pin;
      toast.success('Changed EStop pin to: ' + pin);
      return true;
    } else {
      toast.error(
        'Failed to change EStop pin: ' + (_gpioErrorMessages[data.error] ?? 'Unknown error'),
      );
      return false;
    }
  } catch {
    toast.error('Failed to change EStop pin');
    return false;
  }
}

export async function setEstopEnabled(enabled: boolean): Promise<boolean> {
  try {
    const res = await fetch(
      '/api/config/estop/enabled?' + new URLSearchParams({ enabled: enabled ? '1' : '0' }),
      { method: 'PUT' },
    );
    const data = await res.json();
    if (data.ok) {
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
