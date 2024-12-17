<script lang="ts">
  import '../app.css';
  import Footer from '$lib/components/Layout/Footer.svelte';
  import Header from '$lib/components/Layout/Header.svelte';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { onMount, type Snippet } from 'svelte';
  import { Toaster } from '$lib/components/ui/sonner';

  type Props = {
    children?: Snippet;
  };

  let { children }: Props = $props();

  onMount(() => {
    WebSocketClient.Instance.Connect();
  });
</script>

<Toaster />

<div class="flex min-h-screen flex-col">
  <Header />
  <main class="flex-1">
    {@render children?.()}
  </main>
  <Footer />
</div>
