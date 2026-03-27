/// <reference types="vite/client" />

interface ImportMetaEnv {
  /** Fallback host used when the frontend is served locally (localhost / 127.0.0.1). Defaults to 4.3.2.1. */
  readonly VITE_LOCAL_DEVICE_HOST?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
