<script lang="ts">
  import { HubStateStore } from '$lib/stores';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { WifiAuthMode } from '$lib/_fbs/open-shock/serialization/types/wifi-auth-mode';
  import { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
  import { SerializeWifiScanCommand } from '$lib/Serializers/WifiScanCommand';
  import { SerializeWifiNetworkDisconnectCommand } from '$lib/Serializers/WifiNetworkDisconnectCommand';
  import { SerializeWifiNetworkConnectCommand } from '$lib/Serializers/WifiNetworkConnectCommand';
  import { SerializeWifiNetworkSaveCommand } from '$lib/Serializers/WifiNetworkSaveCommand';
  import WiFiDetailsDialog from '$lib/components/WiFiDetailsDialog.svelte';
  import type { WiFiNetworkGroup } from '$lib/types';
  import { Button } from '$lib/components/ui/button';

  let scanStatus = $derived($HubStateStore.wifiScanStatus);
  let isScanning = $derived(
    scanStatus === WifiScanStatus.Started || scanStatus === WifiScanStatus.InProgress
  );

  let connectedBSSID = $derived($HubStateStore.wifiConnectedBSSID);

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
  function wifiAuthenticate(item: WiFiNetworkGroup) {
    if (item.security !== WifiAuthMode.Open) {
      modalStore.trigger({
        type: 'prompt',
        title: 'Enter password',
        body: 'Enter the password for the network',
        value: '',
        valueAttr: { type: 'password', minlength: 1, maxlength: 63, required: true },
        response: (password: string) => {
          if (!password) return;
          const data = SerializeWifiNetworkSaveCommand(item.ssid, password, true);
          WebSocketClient.Instance.Send(data);
        },
      });
    } else {
      const data = SerializeWifiNetworkSaveCommand(item.ssid, null, true);
      WebSocketClient.Instance.Send(data);
    }
  }
  function wifiConnect(item: WiFiNetworkGroup) {
    const data = SerializeWifiNetworkConnectCommand(item.ssid);
    WebSocketClient.Instance.Send(data);
  }
  function wifiDisconnect(item: WiFiNetworkGroup) {
    const data = SerializeWifiNetworkDisconnectCommand();
    WebSocketClient.Instance.Send(data);
  }
</script>

<div>
  <div class="mb-2 flex items-center justify-between">
    <h3 class="h3">Configure WiFi</h3>
    <Button onclick={wifiScan}>
      {#if isScanning}
        <i class="fa fa-spinner fa-spin"></i>
      {:else}
        <i class="fa fa-rotate-right"></i>
      {/if}
    </Button>
  </div>
  <div class="max-h-64 overflow-auto">
    {#each strengthSortedGroups as [netgroupKey, netgroup] (netgroupKey)}
      <div class="card mb-2 flex items-center justify-between p-2">
        <span>
          {#if netgroup.networks.some((n) => n.bssid === connectedBSSID)}
            <i class="fa fa-wifi text-green-500"></i>
          {:else}
            <i class="fa fa-wifi"></i>
          {/if}
          {#if netgroup.ssid}
            <span class="ml-2">{netgroup.ssid}</span>
          {:else}
            <span class="ml-2">{netgroup.networks[0].bssid}</span><span class="text-gray-500 ml-1"
              >(Hidden)</span
            >
          {/if}
        </span>
        <div class="btn-group variant-outline">
          {#if netgroup.saved}
            <Button onclick={() => wifiConnect(netgroup)}>
              <i class="fa fa-arrow-right text-green-500"></i>
            </Button>
          {:else}
            <Button onclick={() => wifiAuthenticate(netgroup)}>
              <i class="fa fa-link text-green-500"></i>
            </Button>
          {/if}
          <WiFiDetailsDialog group={netgroup} />
        </div>
      </div>
    {/each}
  </div>
</div>
