<script lang="ts">
  import { getModalStore } from '@skeletonlabs/skeleton';
  import { DeviceStateStore } from '$lib/stores';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { WifiAuthMode } from '$lib/_fbs/open-shock/serialization/types/wifi-auth-mode';
  import { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
  import { SerializeWifiScanCommand } from '$lib/Serializers/WifiScanCommand';
  import { SerializeWifiNetworkDisconnectCommand } from '$lib/Serializers/WifiNetworkDisconnectCommand';
  import { SerializeWifiNetworkConnectCommand } from '$lib/Serializers/WifiNetworkConnectCommand';
  import { SerializeWifiNetworkSaveCommand } from '$lib/Serializers/WifiNetworkSaveCommand';
  import WiFiDetails from './modals/WiFiDetails.svelte';
  import type { WiFiNetworkGroup } from '$lib/types';

  const modalStore = getModalStore();

  $: scanStatus = $DeviceStateStore.wifiScanStatus;
  $: isScanning = scanStatus === WifiScanStatus.Started || scanStatus === WifiScanStatus.InProgress;

  $: connectedBSSID = $DeviceStateStore.wifiConnectedBSSID;

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
  function wifiSettings(groupKey: string) {
    modalStore.trigger({
      type: 'component',
      component: {
        ref: WiFiDetails,
        props: { groupKey },
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
    {#each $DeviceStateStore.wifiNetworkGroups as [netgroupKey, netgroup] (netgroupKey)}
      <div class="card mb-2 p-2 flex justify-between items-center">
        <span>
          {#if netgroup.networks.some((n) => n.bssid === connectedBSSID)}
            <i class="fa fa-wifi text-green-500" />
          {:else}
            <i class="fa fa-wifi" />
          {/if}
          {#if netgroup.ssid}
            <span class="ml-2">{netgroup.ssid}</span>
          {:else}
            <span class="ml-2">{netgroup.networks[0].bssid}</span><span class="text-gray-500 ml-1">(Hidden)</span>
          {/if}
        </span>
        <div class="btn-group variant-outline">
          {#if netgroup.saved}
            <button on:click={() => wifiConnect(netgroup)}><i class="fa fa-arrow-right text-green-500" /></button>
          {:else}
            <button on:click={() => wifiAuthenticate(netgroup)}><i class="fa fa-link text-green-500" /></button>
          {/if}
          <button on:click={() => wifiSettings(netgroupKey)}><i class="fa fa-cog" /></button>
        </div>
      </div>
    {/each}
  </div>
</div>
