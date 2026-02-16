<script lang="ts">
  import { HubStateStore } from '$lib/stores';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
  import { SerializeWifiScanCommand } from '$lib/Serializers/WifiScanCommand';
  import { Button } from '$lib/components/ui/button';
  import { LoaderCircle, RotateCcw } from '@lucide/svelte';
  import ScrollArea from './ui/scroll-area/scroll-area.svelte';
  import WiFiEntry from './WiFiEntry.svelte';

  let scanStatus = $derived($HubStateStore.wifiScanStatus);
  let isScanning = $derived(
    scanStatus === WifiScanStatus.Started || scanStatus === WifiScanStatus.InProgress
  );

  // Sorting the groups themselves by each one's strongest network (by RSSI, higher is stronger)
  // Only need to check the first network in each group, since they're already sorted by signal strength
  let strengthSortedGroups = $derived(
    Array.from($HubStateStore.wifiNetworkGroups.entries()).sort(
      (a, b) => b[1].networks[0].rssi - a[1].networks[0].rssi
    )
  );

  function wifiScan() {
    const data = SerializeWifiScanCommand(!isScanning);
    WebSocketClient.Instance.Send(data);
  }
</script>

<div>
  <div class="mb-2 flex items-center justify-between">
    <h3 class="h3">Configure WiFi</h3>
    <Button onclick={wifiScan}>
      {#if isScanning}
        <LoaderCircle class="animate-spin" />
      {:else}
        <RotateCcw />
      {/if}
    </Button>
  </div>
  <ScrollArea class="h-64">
    {#each strengthSortedGroups as [netgroupKey, netgroup] (netgroupKey)}
      <WiFiEntry {netgroup} />
    {/each}
  </ScrollArea>
</div>
