import type { PlaywrightTestConfig } from '@playwright/test';

const config: PlaywrightTestConfig = {
  webServer: {
    command: 'pnpm run build && pnpm run preview',
    port: 4173
  },
  testDir: 'e2e',
  testMatch: /(.+\.)?(test|spec)\.[jt]s/
};

export default config;
