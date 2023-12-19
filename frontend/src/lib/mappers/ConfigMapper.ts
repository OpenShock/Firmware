import type { OtaUpdateChannel } from '$lib/_fbs/open-shock/serialization/configuration';
import { Config as FbsConfig } from '$lib/_fbs/open-shock/serialization/configuration/config';

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

function mapRfConfig(fbsConfig: FbsConfig): RFConfig {
  const rf = fbsConfig.rf();
  if (!rf) throw new Error('fbsConfig.rf is null');

  const txPin = rf.txPin();
  const keepaliveEnabled = rf.keepaliveEnabled();

  if (!txPin) throw new Error('rf.txPin is null');

  return {
    txPin,
    keepaliveEnabled,
  };
}

function mapWifiConfig(fbsConfig: FbsConfig): WifiConfig {
  const wifi = fbsConfig.wifi();
  if (!wifi) throw new Error('fbsConfig.wifi is null');

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
    if (!password) throw new Error('cred.password is null');

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

function mapCaptivePortalConfig(fbsConfig: FbsConfig): CaptivePortalConfig {
  const captivePortal = fbsConfig.captivePortal();
  if (!captivePortal) throw new Error('fbsConfig.captivePortal is null');

  const alwaysEnabled = captivePortal.alwaysEnabled();

  return {
    alwaysEnabled,
  };
}

function mapBackendConfig(fbsConfig: FbsConfig): BackendConfig {
  const backend = fbsConfig.backend();
  if (!backend) throw new Error('fbsConfig.backend is null');

  const domain = backend.domain();
  const authToken = backend.authToken();

  if (!domain) throw new Error('backend.domain is null');

  return {
    domain,
    authToken,
  };
}

function mapSerialInputConfig(fbsConfig: FbsConfig): SerialInputConfig {
  const serialInput = fbsConfig.serialInput();
  if (!serialInput) throw new Error('fbsConfig.serialInput is null');

  const echoEnabled = serialInput.echoEnabled();

  return {
    echoEnabled,
  };
}

function mapOtaUpdateConfig(fbsConfig: FbsConfig): OtaUpdateConfig {
  const otaUpdate = fbsConfig.otaUpdate();
  if (!otaUpdate) throw new Error('fbsConfig.otaUpdate is null');

  const isEnabled = otaUpdate.isEnabled();
  const cdnDomain = otaUpdate.cdnDomain();
  const updateChannel = otaUpdate.updateChannel();
  const checkOnStartup = otaUpdate.checkOnStartup();
  const checkInterval = otaUpdate.checkInterval();
  const allowBackendManagement = otaUpdate.allowBackendManagement();
  const requireManualApproval = otaUpdate.requireManualApproval();

  if (!cdnDomain) throw new Error('otaUpdate.cdnDomain is null');
  if (!updateChannel) throw new Error('otaUpdate.updateChannel is null');

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

export function mapConfig(fbsConfig: FbsConfig | null): Config | null {
  if (!fbsConfig) return null;

  return {
    rf: mapRfConfig(fbsConfig),
    wifi: mapWifiConfig(fbsConfig),
    captivePortal: mapCaptivePortalConfig(fbsConfig),
    backend: mapBackendConfig(fbsConfig),
    serialInput: mapSerialInputConfig(fbsConfig),
    otaUpdate: mapOtaUpdateConfig(fbsConfig),
  };
}
