<script lang="ts">
  import { getModalStore } from '@skeletonlabs/skeleton';
  import type { WiFiNetwork } from '$lib/types/WiFiNetwork';
  import { DeviceStateStore } from '$lib/stores';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { WifiAuthMode } from '$lib/_fbs/open-shock/serialization/types/wifi-auth-mode';
  import { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
  import { SerializeWifiScanCommand } from '$lib/Serializers/WifiScanCommand';
  import { SerializeWifiNetworkDisconnectCommand } from '$lib/Serializers/WifiNetworkDisconnectCommand';
  import { SerializeWifiNetworkConnectCommand } from '$lib/Serializers/WifiNetworkConnectCommand';
  import { SerializeWifiNetworkSaveCommand } from '$lib/Serializers/WifiNetworkSaveCommand';
  import WiFiDetails from './modals/WiFiDetails.svelte';

  const modalStore = getModalStore();

  $: scanStatus = $DeviceStateStore.wifiScanStatus;
  $: isScanning = scanStatus === WifiScanStatus.Started || scanStatus === WifiScanStatus.InProgress;

  let filteredNetworks: WiFiNetwork[] = [];

  // Reactive statement to group networks by SSID and select a representative for each group
  $: {
    const groups = new Map<string, WiFiNetwork[]>();
    $DeviceStateStore.wifiNetworks.forEach((network: WiFiNetwork) => {
      if (!groups.has(network.ssid)) {
        groups.set(network.ssid, []);
      }
      groups.get(network.ssid)?.push(network);
    });

    filteredNetworks = Array.from(groups.values()).map((networks) => {
      // Prioritize network with connected BSSID
      const connectedNetwork = networks.find((n: WiFiNetwork) => n.bssid === connectedBSSID);
      return connectedNetwork || networks[0];
    });
  }
  let connectedBSSID: string | null = null;

  function wifiScan() {
    const data = SerializeWifiScanCommand(!isScanning);
    WebSocketClient.Instance.Send(data);
  }
  function wifiAuthenticate(item: WiFiNetwork) {
    if (item.security !== WifiAuthMode.Open) {
      modalStore.trigger({
        type: 'prompt',
        title: 'Enter password',
        body: 'Enter the password for the network',
        value: '',
        valueAttr: { type: 'password', minlength: 1, maxlength: 63, required: true },
        response: (password: string) => {
          const data = SerializeWifiNetworkSaveCommand(item.ssid, password, true);
          WebSocketClient.Instance.Send(data);
        },
      });
    } else {
      const data = SerializeWifiNetworkSaveCommand(item.ssid, null, true);
      WebSocketClient.Instance.Send(data);
    }
  }
  function wifiConnect(item: WiFiNetwork) {
    const data = SerializeWifiNetworkConnectCommand(item.ssid);
    WebSocketClient.Instance.Send(data);
  }
  function wifiDisconnect(item: WiFiNetwork) {
    const data = SerializeWifiNetworkDisconnectCommand();
    WebSocketClient.Instance.Send(data);
  }
  function wifiSettings(item: WiFiNetwork) {
    modalStore.trigger({
      type: 'component',
      component: {
        ref: WiFiDetails,
        props: { bssid: item.bssid },
      },
    });
  }
</script>

<div>
  <div class="flex justify-between items-center mb-2">
    <h3 class="h3">Configure WiFi</h3>
    <button class="btn variant-outline" on:click={wifiScan}>
      {#if isScanning}
        <i class="fa fa-spinner fa-spin"></i>
      {:else}
        <i class="fa fa-rotate-right"></i>
      {/if}
    </button>
  </div>
  <div class="max-h-64 overflow-auto">
    {#each filteredNetworks as network}
      <div class="card mb-2 p-2 flex justify-between items-center">
        <span>
          {#if network.bssid === connectedBSSID}
            <i class="fa fa-wifi text-green-500" />
          {:else}
            <i class="fa fa-wifi" />
          {/if}
          {#if network.ssid}
            <span class="ml-2">{network.ssid}</span>
          {:else}
            <span class="ml-2">{network.bssid}</span><span class="text-gray-500 ml-1">(Hidden)</span>
          {/if}
        </span>
        <div class="btn-group variant-outline">
          {#if network.saved}
            <button on:click={() => wifiConnect(network)}><i class="fa fa-arrow-right text-green-500" /></button>
          {:else}
            <button on:click={() => wifiAuthenticate(network)}><i class="fa fa-link text-green-500" /></button>
          {/if}
          <button on:click={() => wifiSettings(network)}><i class="fa fa-cog" /></button>
        </div>
      </div>
    {/each}
  </div>
</div>
