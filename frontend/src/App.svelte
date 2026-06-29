<script lang="ts">
  import { WebSocketClient } from '$lib/WebSocketClient';
  import Header from '$lib/components/Layout/Header.svelte';
  import Landing from '$lib/views/Landing.svelte';
  import Guided from '$lib/views/Guided.svelte';
  import Advanced from '$lib/views/Advanced.svelte';
  import Success from '$lib/views/Success.svelte';
  import { Toaster } from '$lib/components/ui/sonner';
  import { initializeDarkModeStore, ViewModeStore } from '$lib/stores';
  import { closePortal } from '$lib/portalClose';
  import { fetchBoardInfo } from '$lib/api';
  import { onMount } from 'svelte';

  onMount(() => {
    initializeDarkModeStore();
    fetchBoardInfo();
    WebSocketClient.Instance.Connect();
  });

  let showSuccess = $state(false);
</script>

<Toaster position="top-center" />

{#if showSuccess}
  <Success onClose={closePortal} />
{:else}
  <div class="flex min-h-screen flex-col">
    {#if $ViewModeStore === 'landing'}
      <Landing />
    {:else}
      <Header />
      {#if $ViewModeStore === 'advanced'}
        <Advanced />
      {:else}
        <Guided onComplete={() => (showSuccess = true)} />
      {/if}
    {/if}
  </div>
{/if}
