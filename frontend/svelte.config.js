import adapter from '@sveltejs/adapter-static';
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';

/** @type {import('@sveltejs/kit').Config} */
const config = {
  preprocess: vitePreprocess(),

  compilerOptions: {
    runes: true,
    modernAst: true,
  },

  kit: {
    adapter: adapter(),
    output: {
      bundleStrategy: 'inline',
    },
  },
};

export default config;
