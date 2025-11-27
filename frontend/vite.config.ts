import { defineConfig, type UserConfig } from 'vite';
import { sveltekit } from '@sveltejs/kit/vite';
import tailwindcss from '@tailwindcss/vite';

export default defineConfig({
  plugins: [sveltekit(), tailwindcss()],

  test: {
    include: ['src/**/*.{test,spec}.{js,ts}'],
  },
} as UserConfig); // TODO: "test" is not a valid property of the defineconfig argument? This needs to get fixed
