import { svelte, vitePreprocess } from '@sveltejs/vite-plugin-svelte';
import tailwindcss from '@tailwindcss/vite';
import license from 'rollup-plugin-license';
import { visualizer } from 'rollup-plugin-visualizer';
import type { ESBuildOptions, Plugin } from 'vite';
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
      // svelte-core is a workspace package with its own node_modules, so without this its
      // components bundle whatever versions its devDependencies resolve to. Resolve its
      // runtime peers from this app so the versions pinned in package.json are what ships
      // (e.g. tailwind-variants >=3.3 vendors tailwind-merge and adds ~80 kB).
      dedupe: [
        'bits-ui',
        'svelte-sonner',
        '@lucide/svelte',
        '@internationalized/date',
        'tailwind-merge',
        'tailwind-variants',
        'clsx',
      ],
    },

    publicDir: 'static',

    build: {
      target: 'es2024',
      outDir: 'build',
    },

    plugins: [
      svelte({
        // Inline instead of svelte.config.js, matching the cloud frontend's SvelteKit 3 setup.
        preprocess: vitePreprocess(),
        compilerOptions: {
          runes: true,
          modernAst: true,
        },
      }),
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
    } as ESBuildOptions,

    test: {
      // A test that runs no assertions fails instead of silently passing.
      expect: { requireAssertions: true },
      include: ['src/**/*.{test,spec}.{js,ts}'],
    },
  };
});
