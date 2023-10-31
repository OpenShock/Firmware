<script lang="ts">
  import { SerializeWifiNetworkSaveCommand } from '$lib/Serializers/WifiNetworkSaveCommand';
  import { SerializeWifiNetworkConnectCommand } from '$lib/Serializers/WifiNetworkConnectCommand';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { DeviceStateStore } from '$lib/stores';
  import { getModalStore } from '@skeletonlabs/skeleton';
  import { SerializeWifiNetworkForgetCommand } from '$lib/Serializers/WifiNetworkForgetCommand';

  export let bssid: string;

  const modalStore = getModalStore();

  $: item = $DeviceStateStore.wifiNetworks.get(bssid);

  $: rows = item
    ? [
        { key: 'SSID', value: item.ssid },
        { key: 'BSSID', value: item.bssid },
        { key: 'Channel', value: item.channel },
        { key: 'RSSI', value: item.rssi },
        { key: 'Security', value: item.security },
        { key: 'Saved', value: item.saved },
      ]
    : [];

  function AuthenticateWiFi() {
    if (!item) return;
    // TODO: Prompt for password
    const data = SerializeWifiNetworkSaveCommand(item.ssid, '', true);
    WebSocketClient.Instance.Send(data);
    modalStore.close();
  }
  function ConnectWiFi() {
    if (!item) return;
    const data = SerializeWifiNetworkConnectCommand(item.ssid);
    WebSocketClient.Instance.Send(data);
    modalStore.close();
  }
  function ForgetWiFi() {
    if (!item) return;
    const data = SerializeWifiNetworkForgetCommand(item.ssid);
    WebSocketClient.Instance.Send(data);
    modalStore.close();
  }
</script>

<div class="card p-4 w-[24rem] flex-col space-y-4">
  {#if item}
    <div class="flex justify-between space-x-2">
      <h2 class="h2">WiFi Info</h2>
      <button class="btn-icon variant-outline" on:click={() => modalStore.close()}><i class="fa fa-xmark"></i></button>
    </div>
    <div>
      {#each rows as row (row.key)}
        <span class="flex justify-between"><span class="font-bold">{row.key}:</span><span class="text-gray-300">{row.value}</span></span>
      {/each}
    </div>
    <div class="flex justify-end space-x-2">
      <div class="btn-group variant-outline">
        {#if item.saved}
          <button on:click={ConnectWiFi}><i class="fa fa-wifi mr-2 text-green-500"></i>Connect</button>
          <button on:click={ForgetWiFi}><i class="fa fa-trash mr-2 text-red-500"></i>Forget</button>
        {:else}
          <button on:click={AuthenticateWiFi}><i class="fa fa-link mr-2 text-green-500"></i>Connect</button>
        {/if}
      </div>
    </div>
  {:else}
    <div class="flex justify-between space-x-2">
      <h2 class="h2">WiFi Info</h2>
      <button class="btn-icon variant-outline" on:click={() => modalStore.close()}><i class="fa fa-xmark"></i></button>
    </div>
    <div class="flex justify-center">
      <i class="fa fa-spinner fa-spin"></i>
    </div>
  {/if}
</div>
