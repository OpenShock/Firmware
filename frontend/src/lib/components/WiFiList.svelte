<script lang="ts">
  import { HubStateStore } from '$lib/stores';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
  import { SerializeWifiScanCommand } from '$lib/Serializers/WifiScanCommand';
  import { SerializeWifiNetworkDisconnectCommand } from '$lib/Serializers/WifiNetworkDisconnectCommand';
  import { SerializeWifiNetworkConnectCommand } from '$lib/Serializers/WifiNetworkConnectCommand';
  import { SerializeWifiNetworkSaveCommand } from '$lib/Serializers/WifiNetworkSaveCommand';
  import type { WiFiNetworkGroup } from '$lib/types';
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

  let dialogOpen = $state(false);
  let pendingPassword = $state<string | null>(null);

  function wifiScan() {
    const data = SerializeWifiScanCommand(!isScanning);
    WebSocketClient.Instance.Send(data);
  }
  function wifiAuthenticate(ssid: string, password: string | null) {
    dialogOpen = false;
    const data = SerializeWifiNetworkSaveCommand(ssid, null, true);
    WebSocketClient.Instance.Send(data);
  }
  function wifiConnect(item: WiFiNetworkGroup) {
    const data = SerializeWifiNetworkConnectCommand(item.ssid);
    WebSocketClient.Instance.Send(data);
  }
  function wifiDisconnect(item: WiFiNetworkGroup) {
    const data = SerializeWifiNetworkDisconnectCommand();
    WebSocketClient.Instance.Send(data);
  }

  function handleWifiAuthDialogOpenChange(open: boolean) {
    dialogOpen = open;
    if (!open) {
      pendingPassword = null;
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
