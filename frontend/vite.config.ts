import { sveltekit } from '@sveltejs/kit/vite';
import tailwindcss from '@tailwindcss/vite';
import { defineConfig } from 'vite';
import devtoolsJson from 'vite-plugin-devtools-json';

export default defineConfig({
  build: {
    target: 'es2022',
  },
  plugins: [sveltekit(), tailwindcss(), devtoolsJson()],
  test: {
    include: ['src/**/*.{test,spec}.{js,ts}'],
  },
});
