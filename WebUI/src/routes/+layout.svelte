<script lang="ts">
  import '../app.postcss';
  import { computePosition, autoUpdate, flip, shift, offset, arrow } from '@floating-ui/dom';
  import { AppShell, Modal, Toast, getToastStore, initializeStores, storePopup } from '@skeletonlabs/skeleton';
  import Footer from '$lib/components/Layout/Footer.svelte';
  import Header from '$lib/components/Layout/Header.svelte';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { toastDelegator } from '$lib/stores/ToastDelegator';

  storePopup.set({ computePosition, autoUpdate, flip, shift, offset, arrow });
  initializeStores();

  const toastStore = getToastStore();

  WebSocketClient.Instance.Connect();

  $: if ($toastDelegator.length > 0) {
    const actions = $toastDelegator;

    for (let i = 0; i < actions.length; i++) {
      const action = actions[i];
      if (action.type === 'trigger') {
        toastStore.trigger(action.toast);
      } else if (action.type === 'clear') {
        toastStore.clear();
      }
    }
    $toastDelegator = [];
  }
</script>

<Modal />
<Toast position="bl" max={5} />

<AppShell>
  <Header slot="header" />
  <slot />
  <Footer slot="pageFooter" />
</AppShell>
