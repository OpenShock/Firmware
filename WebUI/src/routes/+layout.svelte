<script lang="ts">
	import '../app.postcss';
	import { computePosition, autoUpdate, flip, shift, offset, arrow } from '@floating-ui/dom';
  import { AppShell, Modal, Toast, initializeStores, storePopup } from '@skeletonlabs/skeleton';
  import Footer from '$lib/components/Layout/Footer.svelte';
  import Header from '$lib/components/Layout/Header.svelte';
	import { WebSocketClient } from '$lib/WebSocketClient';

	storePopup.set({ computePosition, autoUpdate, flip, shift, offset, arrow });
  initializeStores();

  WebSocketClient.Instance.Connect();
</script>

<Modal />
<Toast position="bl" max={5} />

<AppShell>
  <Header slot="header" />
  <slot />
  <Footer slot="pageFooter" />
</AppShell>
