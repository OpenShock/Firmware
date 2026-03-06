<script lang="ts">
  import { hubState } from '$lib/stores';
  import { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
  import { startWifiScan, stopWifiScan, disconnectWifiNetwork } from '$lib/api';
  import { Button } from '$lib/components/ui/button';
  import ScrollArea from '$lib/components/ui/scroll-area/scroll-area.svelte';
  import WiFiEntry from '$lib/components/WiFiEntry.svelte';
  import AddHiddenNetworkDialog from '$lib/components/AddHiddenNetworkDialog.svelte';
  import { LoaderCircle, RotateCcw, Wifi, WifiOff } from '@lucide/svelte';

  let scanStatus = $derived(hubState.wifiScanStatus);
  let isScanning = $derived(
    scanStatus === WifiScanStatus.Started || scanStatus === WifiScanStatus.InProgress
  );
  let connectedBSSID = $derived(hubState.wifiConnectedBSSID);

  let strengthSortedGroups = $derived(
    Array.from(hubState.wifiNetworkGroups.entries()).sort(
      (a, b) => b[1].networks[0].rssi - a[1].networks[0].rssi
    )
  );

  let savedGroups = $derived(strengthSortedGroups.filter(([, g]) => g.saved));
  let availableGroups = $derived(strengthSortedGroups.filter(([, g]) => !g.saved));

  let connectedNetwork = $derived.by(() => {
    if (!connectedBSSID) return null;
    for (const [, group] of strengthSortedGroups) {
      if (group.networks.some((n) => n.bssid === connectedBSSID)) {
        return group;
      }
    }
    return null;
  });

  async function wifiScan() {
    if (isScanning) {
      await stopWifiScan();
    } else {
      await startWifiScan();
    }
  }

  function wifiDisconnect() {
    disconnectWifiNetwork();
  }
</script>

<div class="flex flex-col gap-4">
  <!-- Connection Status -->
  {#if connectedNetwork}
    <div
      class="flex items-center justify-between rounded-lg border border-green-500/30 bg-green-500/10 p-3"
    >
      <div class="flex items-center gap-2">
        <Wifi class="h-5 w-5 text-green-500" />
        <div>
          <p class="text-sm font-medium">
            Connected to {connectedNetwork.ssid || 'Hidden Network'}
          </p>
          <p class="text-muted-foreground text-xs">
            {connectedNetwork.networks[0]?.rssi ?? '?'} dBm
          </p>
        </div>
      </div>
      <Button variant="outline" size="sm" onclick={wifiDisconnect}>
        <WifiOff class="mr-1 h-4 w-4" />
        Disconnect
      </Button>
    </div>
  {:else}
    <div
      class="flex items-center gap-2 rounded-lg border border-yellow-500/30 bg-yellow-500/10 p-3"
    >
      <WifiOff class="h-5 w-5 text-yellow-500" />
      <p class="text-sm text-yellow-700 dark:text-yellow-300">Not connected to any network</p>
    </div>
  {/if}

  <!-- Saved Networks -->
  {#if savedGroups.length > 0}
    <div>
      <h4 class="text-muted-foreground mb-2 text-sm font-medium">Saved Networks</h4>
      {#each savedGroups as [netgroupKey, netgroup] (netgroupKey)}
        <WiFiEntry {netgroup} />
      {/each}
    </div>
  {/if}

  <!-- Available Networks -->
  <div>
    <div class="mb-2 flex items-center justify-between">
      <h4 class="text-muted-foreground text-sm font-medium">Available Networks</h4>
      <div class="flex items-center gap-2">
        <AddHiddenNetworkDialog />
        <Button
          variant="outline"
          size="icon"
          onclick={wifiScan}
          title={isScanning ? 'Stop scan' : 'Scan'}
        >
          {#if isScanning}
            <LoaderCircle class="h-4 w-4 animate-spin" />
          {:else}
            <RotateCcw class="h-4 w-4" />
          {/if}
        </Button>
      </div>
    </div>
    <ScrollArea class="h-52">
      {#if availableGroups.length > 0}
        {#each availableGroups as [netgroupKey, netgroup] (netgroupKey)}
          <WiFiEntry {netgroup} />
        {/each}
      {:else if !isScanning}
        <p class="text-muted-foreground py-4 text-center text-sm">
          No networks found. Tap scan to search.
        </p>
      {:else}
        <p class="text-muted-foreground py-4 text-center text-sm">Scanning...</p>
      {/if}
    </ScrollArea>
  </div>
</div>
