import { sveltekit } from '@sveltejs/kit/vite';
import tailwindcss from '@tailwindcss/vite';
import license from 'rollup-plugin-license';
import { visualizer } from 'rollup-plugin-visualizer';
import { defineConfig } from 'vitest/config';

export default defineConfig(({ mode }) => {
  const analyze = process.env.ANALYZE === 'true';
  const isProduction = mode === 'production';

  return {
    build: {
      target: 'es2022',
    },
    plugins: [
      sveltekit(),
      tailwindcss(),
      license({
        thirdParty: {
          includePrivate: true,
          includeSelf: true,
          multipleVersions: true,
          output: {
            file: './.svelte-kit/output/client/LICENSES.txt',
          },
        },
      }),

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

    esbuild: {
      legalComments: 'none',
      banner: '/*! For license information, see LICENSES.txt */',
      drop: isProduction ? ['debugger'] : [],
      pure: isProduction ? ['console.log', 'console.debug', 'console.trace'] : [],
    },

    test: {
      include: ['src/**/*.{test,spec}.{js,ts}'],
    },
  };
});
