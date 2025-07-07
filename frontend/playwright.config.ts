import { defineConfig } from '@playwright/test';

export default defineConfig({
  reporter: process.env.CI ? 'github' : 'html',
  webServer: {
    command: 'pnpm run build && pnpm run preview',
    port: 4173,
    reuseExistingServer: !process.env.CI,
  },
  testDir: 'e2e',
});
