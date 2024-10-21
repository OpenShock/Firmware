<script lang="ts">
  import { run } from 'svelte/legacy';

  import '../app.postcss';
  import { computePosition, autoUpdate, flip, shift, offset, arrow } from '@floating-ui/dom';
  import { AppShell, Modal, Toast, getToastStore, initializeStores, storePopup } from '@skeletonlabs/skeleton';
  import Footer from '$lib/components/Layout/Footer.svelte';
  import Header from '$lib/components/Layout/Header.svelte';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { toastDelegator } from '$lib/stores/ToastDelegator';
  interface Props {
    children?: import('svelte').Snippet;
  }

  let { children }: Props = $props();

  storePopup.set({ computePosition, autoUpdate, flip, shift, offset, arrow });
  initializeStores();

  const toastStore = getToastStore();

  WebSocketClient.Instance.Connect();

  run(() => {
    if ($toastDelegator.length > 0) {
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
  });
</script>

<Modal />
<Toast position="bl" max={5} />

<AppShell>
  {#snippet header()}
    <Header  />
  {/snippet}
  {@render children?.()}
  {#snippet pageFooter()}
    <Footer  />
  {/snippet}
</AppShell>
