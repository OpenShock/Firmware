<script lang="ts">
  import { hubState } from '$lib/stores';
  import { startWifiScan, stopWifiScan } from '$lib/api';
  import { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
  import { Button } from '$lib/components/ui/button';
  import { LoaderCircle, Radar } from '@lucide/svelte';
  import ScrollArea from './ui/scroll-area/scroll-area.svelte';
  import WiFiEntry from './WiFiEntry.svelte';

  let scanStatus = $derived(hubState.wifiScanStatus);
  let isScanning = $derived(
    scanStatus === WifiScanStatus.Started || scanStatus === WifiScanStatus.InProgress
  );

  let strengthSortedGroups = $derived(
    Array.from(hubState.wifiNetworkGroups.entries()).sort(
      (a, b) => b[1].networks[0].rssi - a[1].networks[0].rssi
    )
  );

  async function wifiScan() {
    if (isScanning) {
      await stopWifiScan();
    } else {
      await startWifiScan();
    }
  }
</script>

<div>
  <div class="mb-2 flex items-center justify-between">
    <h3 class="h3">Configure WiFi</h3>
    <Button onclick={wifiScan}>
      {#if isScanning}
        <LoaderCircle class="animate-spin" />
      {:else}
        <Radar />
      {/if}
    </Button>
  </div>
  <ScrollArea class="h-64">
    {#each strengthSortedGroups as [netgroupKey, netgroup] (netgroupKey)}
      <WiFiEntry ssid={netgroup.ssid} {netgroup} />
    {/each}
  </ScrollArea>
</div>
