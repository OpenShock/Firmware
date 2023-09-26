<script lang="ts">
	import '../app.postcss';
	import { computePosition, autoUpdate, flip, shift, offset, arrow } from '@floating-ui/dom';
  import { AppShell, Toast, initializeStores, storePopup } from '@skeletonlabs/skeleton';
  import Footer from '$lib/components/Layout/Footer.svelte';
  import Header from '$lib/components/Layout/Header.svelte';

	storePopup.set({ computePosition, autoUpdate, flip, shift, offset, arrow });
  initializeStores();
</script>

<Toast position="bl" max={5} />

<AppShell>
  <Header slot="header" />
  <slot />
  <Footer slot="pageFooter" />
</AppShell>
