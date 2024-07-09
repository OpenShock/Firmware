import type { OtaUpdateChannel } from '$lib/_fbs/open-shock/serialization/configuration';
import { HubConfig } from '$lib/_fbs/open-shock/serialization/configuration/hub-config';

// TODO: Update these configs and ensure that typescript enforces them to be up to date

export interface RFConfig {
  txPin: number;
  keepaliveEnabled: boolean;
}

export interface WifiCredentials {
  id: number;
  ssid: string;
  password: string | null;
}

export interface WifiConfig {
  apSsid: string;
  hostname: string;
  credentials: WifiCredentials[];
}

export interface CaptivePortalConfig {
  alwaysEnabled: boolean;
}

export interface BackendConfig {
  domain: string;
  authToken: string | null;
}

export interface SerialInputConfig {
  echoEnabled: boolean;
}

export interface OtaUpdateConfig {
  isEnabled: boolean;
  cdnDomain: string;
  updateChannel: OtaUpdateChannel;
  checkOnStartup: boolean;
  checkInterval: number;
  allowBackendManagement: boolean;
  requireManualApproval: boolean;
}

export interface Config {
  rf: RFConfig;
  wifi: WifiConfig;
  captivePortal: CaptivePortalConfig;
  backend: BackendConfig;
  serialInput: SerialInputConfig;
  otaUpdate: OtaUpdateConfig;
}

function mapRfConfig(hubConfig: HubConfig): RFConfig {
  const rf = hubConfig.rf();
  if (!rf) throw new Error('hubConfig.rf is null');

  const txPin = rf.txPin();
  const keepaliveEnabled = rf.keepaliveEnabled();

  return {
    txPin,
    keepaliveEnabled,
  };
}

function mapWifiConfig(hubConfig: HubConfig): WifiConfig {
  const wifi = hubConfig.wifi();
  if (!wifi) throw new Error('hubConfig.wifi is null');

  const apSsid = wifi.apSsid();
  const hostname = wifi.hostname();

  if (!apSsid) throw new Error('wifi.apSsid is null');
  if (!hostname) throw new Error('wifi.hostname is null');

  const credentials: WifiCredentials[] = [];
  const credentialsLength = wifi.credentialsLength();
  for (let i = 0; i < credentialsLength; i++) {
    const cred = wifi.credentials(i);
    if (!cred) throw new Error('wifi.credentials is null');

    const id = cred.id();
    const ssid = cred.ssid();
    const password = cred.password();

    if (!id) throw new Error('cred.id is null');
    if (!ssid) throw new Error('cred.ssid is null');

    credentials.push({
      id,
      ssid,
      password,
    });
  }

  return {
    apSsid,
    hostname,
    credentials,
  };
}

function mapCaptivePortalConfig(hubConfig: HubConfig): CaptivePortalConfig {
  const captivePortal = hubConfig.captivePortal();
  if (!captivePortal) throw new Error('hubConfig.captivePortal is null');

  const alwaysEnabled = captivePortal.alwaysEnabled();

  return {
    alwaysEnabled,
  };
}

function mapBackendConfig(hubConfig: HubConfig): BackendConfig {
  const backend = hubConfig.backend();
  if (!backend) throw new Error('hubConfig.backend is null');

  const domain = backend.domain();
  const authToken = backend.authToken();

  if (!domain) throw new Error('backend.domain is null');

  return {
    domain,
    authToken,
  };
}

function mapSerialInputConfig(hubConfig: HubConfig): SerialInputConfig {
  const serialInput = hubConfig.serialInput();
  if (!serialInput) throw new Error('hubConfig.serialInput is null');

  const echoEnabled = serialInput.echoEnabled();

  return {
    echoEnabled,
  };
}

function mapOtaUpdateConfig(hubConfig: HubConfig): OtaUpdateConfig {
  const otaUpdate = hubConfig.otaUpdate();
  if (!otaUpdate) throw new Error('hubConfig.otaUpdate is null');

  const isEnabled = otaUpdate.isEnabled();
  const cdnDomain = otaUpdate.cdnDomain();
  const updateChannel = otaUpdate.updateChannel();
  const checkOnStartup = otaUpdate.checkOnStartup();
  const checkInterval = otaUpdate.checkInterval();
  const allowBackendManagement = otaUpdate.allowBackendManagement();
  const requireManualApproval = otaUpdate.requireManualApproval();

  if (!cdnDomain) throw new Error('otaUpdate.cdnDomain is null');

  return {
    isEnabled,
    cdnDomain,
    updateChannel,
    checkOnStartup,
    checkInterval,
    allowBackendManagement,
    requireManualApproval,
  };
}

export function mapConfig(hubConfig: HubConfig | null): Config | null {
  if (!hubConfig) return null;

  return {
    rf: mapRfConfig(hubConfig),
    wifi: mapWifiConfig(hubConfig),
    captivePortal: mapCaptivePortalConfig(hubConfig),
    backend: mapBackendConfig(hubConfig),
    serialInput: mapSerialInputConfig(hubConfig),
    otaUpdate: mapOtaUpdateConfig(hubConfig),
  };
}
