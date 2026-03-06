import { sveltekit } from '@sveltejs/kit/vite';
import tailwindcss from '@tailwindcss/vite';
import { visualizer } from 'rollup-plugin-visualizer';
import { defineConfig } from 'vitest/config';

export default defineConfig(() => {
  const analyze = process.env.ANALYZE === 'true';

  return {
    plugins: [
      sveltekit(),
      tailwindcss(),

      ...(analyze
        ? [
            visualizer({
              filename: 'dist/stats.html',
              template: 'treemap',
              gzipSize: true,
              brotliSize: true,
              open: true,
            }),
            visualizer({
              filename: 'dist/stats.json',
              template: 'raw-data',
            }),
          ]
        : []),
    ],

    test: {
      include: ['src/**/*.{test,spec}.{js,ts}'],
    },
  };
});
