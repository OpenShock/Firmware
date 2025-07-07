<script lang="ts">
  import '../app.css';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import Footer from '$lib/components/Layout/Footer.svelte';
  import Header from '$lib/components/Layout/Header.svelte';
  import { Toaster } from '$lib/components/ui/sonner';
  import { initializeDarkModeStore } from '$lib/stores/ColorSchemeStore';
  import { type Snippet, onMount } from 'svelte';

  interface Props {
    children?: Snippet;
  }

  let { children }: Props = $props();

  onMount(() => {
    initializeDarkModeStore();
    WebSocketClient.Instance.Connect();
  });
</script>

<Toaster />

<div class="flex min-h-screen flex-col">
  <Header />
  {@render children?.()}
  <Footer />
</div>
