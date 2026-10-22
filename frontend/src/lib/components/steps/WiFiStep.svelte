<script lang="ts">
  import SectionHeader from '#lib/components/SectionHeader.svelte';
  import WiFiManager from '#lib/components/WiFiManager.svelte';
  import { hubState } from '#lib/stores/index.js';
  import { CircleCheck } from '@lucide/svelte';

  let isConnected = $derived(hubState.wifiConnectedBSSID !== null);
</script>

<div class="flex flex-col gap-4">
  <SectionHeader title="WiFi Setup" description="Connect the hub to your WiFi network.">
    {#if isConnected}
      <CircleCheck class="text-success h-6 w-6 shrink-0" />
    {/if}
  </SectionHeader>

  <WiFiManager />
</div>
