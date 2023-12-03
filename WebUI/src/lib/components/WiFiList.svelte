<script lang="ts">
  import { getModalStore } from '@skeletonlabs/skeleton';
  import type { WiFiNetwork } from '$lib/types/WiFiNetwork';
  import { DeviceStateStore } from '$lib/stores';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { WifiAuthMode } from '$lib/_fbs/open-shock/serialization/types/wifi-auth-mode';
  import { SerializeWifiNetworkDisconnectCommand } from '$lib/Serializers/WifiNetworkDisconnectCommand';
  import { SerializeWifiNetworkConnectCommand } from '$lib/Serializers/WifiNetworkConnectCommand';
  import { SerializeWifiNetworkSaveCommand } from '$lib/Serializers/WifiNetworkSaveCommand';
  import WiFiDetails from './modals/WiFiDetails.svelte';

  const modalStore = getModalStore();

  let connectedBSSID: string | null = null;

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

<div class="card overflow-auto p-2">
  {#if $DeviceStateStore.wifiNetworksPresent.size === 0}
    <h3 class="h3 text-center my-4">No WiFi Networks Found</h3>
  {/if}
  {#each $DeviceStateStore.wifiNetworksPresent as [key, network] (key)}
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
