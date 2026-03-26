import { svelte } from '@sveltejs/vite-plugin-svelte';
import tailwindcss from '@tailwindcss/vite';
import { fileURLToPath } from 'node:url';
import license from 'rollup-plugin-license';
import { visualizer } from 'rollup-plugin-visualizer';
import type { Plugin } from 'vite';
import { viteSingleFile } from 'vite-plugin-singlefile';
import { defineConfig } from 'vitest/config';

function jsBannerPlugin(banner: string): Plugin {
  return {
    name: 'js-banner',
    enforce: 'post',
    generateBundle(_, bundle) {
      for (const chunk of Object.values(bundle)) {
        if (chunk.type === 'chunk') {
          chunk.code = banner + '\n' + chunk.code;
        }
      }
    },
  };
}

export default defineConfig(({ mode }) => {
  const analyze = process.env.ANALYZE === 'true';
  const isProduction = mode === 'production';

  return {
    resolve: {
      alias: {
        $lib: fileURLToPath(new URL('./src/lib', import.meta.url)),
      },
    },

    publicDir: 'static',

    build: {
      target: 'es2024',
      outDir: 'build',
    },

    plugins: [
      svelte(),
      tailwindcss(),
      jsBannerPlugin('/*! For license information, see LICENSES.txt */'),
      viteSingleFile(),
      license({
        thirdParty: {
          includePrivate: true,
          includeSelf: true,
          multipleVersions: true,
          output: {
            file: './build/LICENSES.txt',
          },
        },
      }),

      ...(analyze
        ? [
            visualizer({
              filename: 'build/stats.html',
              template: 'treemap',
              gzipSize: true,
              brotliSize: true,
              open: true,
            }),
            visualizer({
              filename: 'build/stats.json',
              template: 'raw-data',
            }),
          ]
        : []),
    ],

    esbuild: {
      legalComments: 'none',
      drop: isProduction ? ['debugger'] : [],
      pure: isProduction ? ['console.log', 'console.debug', 'console.trace'] : [],
    },

    test: {
      include: ['src/**/*.{test,spec}.{js,ts}'],
    },
  };
});
