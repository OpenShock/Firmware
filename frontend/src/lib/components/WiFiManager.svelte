<script lang="ts">
  import { hubState } from '#lib/stores/index.js';
  import { WifiScanStatus } from '#lib/_fbs/open-shock/serialization/types/wifi-scan-status.js';
  import { startWifiScan, stopWifiScan, disconnectWifiNetwork } from '#lib/api.js';
  import { Button } from '@openshock/svelte-core/components/ui/button';
  import { ScrollArea } from '@openshock/svelte-core/components/ui/scroll-area';
  import WiFiEntry from '#lib/components/WiFiEntry.svelte';
  import AddHiddenNetworkDialog from '#lib/components/AddHiddenNetworkDialog.svelte';
  import { LoaderCircle, Radar, Wifi, WifiOff } from '@lucide/svelte';

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
  let savedOnlySSIDs = $derived(hubState.savedOnlySSIDs);
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
      class="border-success/30 bg-success/10 flex items-center justify-between rounded-lg border p-3"
    >
      <div class="flex items-center gap-2">
        <Wifi class="text-success h-5 w-5" />
        <div>
          <p class="text-sm font-medium">
            Connected to {connectedNetwork.ssid || 'hidden network'}
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
    <div class="border-warning/30 bg-warning/10 flex items-center gap-2 rounded-lg border p-3">
      <WifiOff class="text-warning h-5 w-5" />
      <p class="text-sm">Not connected to any network</p>
    </div>
  {/if}

  <!-- Saved Networks -->
  {#if savedGroups.length > 0 || savedOnlySSIDs.length > 0}
    <div>
      <h4 class="text-muted-foreground mb-2 text-sm font-medium">Saved Networks</h4>
      {#each savedGroups as [netgroupKey, netgroup] (netgroupKey)}
        <WiFiEntry ssid={netgroup.ssid} {netgroup} />
      {/each}
      {#each savedOnlySSIDs as ssid (ssid)}
        <WiFiEntry {ssid} />
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
            <Radar class="h-4 w-4" />
          {/if}
        </Button>
      </div>
    </div>
    <ScrollArea class="h-52">
      {#if availableGroups.length > 0}
        {#each availableGroups as [netgroupKey, netgroup] (netgroupKey)}
          <WiFiEntry ssid={netgroup.ssid} {netgroup} />
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
