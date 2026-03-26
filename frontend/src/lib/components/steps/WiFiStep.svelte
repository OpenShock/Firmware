<script lang="ts">
  import WiFiManager from '$lib/components/WiFiManager.svelte';
  import { hubState } from '$lib/stores';
  import { CircleCheck } from '@lucide/svelte';

  let isConnected = $derived(hubState.wifiConnectedBSSID !== null);
</script>

<div class="flex flex-col gap-4">
  <div class="flex items-center justify-between">
    <div>
      <h3 class="text-lg font-semibold">WiFi Setup</h3>
      <p class="text-muted-foreground text-sm">Connect to your WiFi network to continue</p>
    </div>
    {#if isConnected}
      <CircleCheck class="h-6 w-6 text-green-500" />
    {/if}
  </div>

  <WiFiManager />
</div>
