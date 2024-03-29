<script lang="ts">
  import { SerializeWifiNetworkSaveCommand } from '$lib/Serializers/WifiNetworkSaveCommand';
  import { SerializeWifiNetworkConnectCommand } from '$lib/Serializers/WifiNetworkConnectCommand';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { DeviceStateStore } from '$lib/stores';
  import { getModalStore } from '@skeletonlabs/skeleton';
  import { SerializeWifiNetworkForgetCommand } from '$lib/Serializers/WifiNetworkForgetCommand';
  import { WifiAuthMode } from '$lib/_fbs/open-shock/serialization/types/wifi-auth-mode';

  const modalStore = getModalStore();

  export let groupKey: string;
  $: group = $DeviceStateStore.wifiNetworkGroups.get(groupKey);

  function GetWifiAuthModeString(type: WifiAuthMode) {
    switch (type) {
      case WifiAuthMode.Open:
        return 'Open';
      case WifiAuthMode.WEP:
        return 'WEP';
      case WifiAuthMode.WPA_PSK:
        return 'WPA PSK';
      case WifiAuthMode.WPA2_PSK:
        return 'WPA2 PSK';
      case WifiAuthMode.WPA_WPA2_PSK:
        return 'WPA/WPA2 PSK';
      case WifiAuthMode.WPA2_ENTERPRISE:
        return 'WPA2 Enterprise';
      case WifiAuthMode.WPA3_PSK:
        return 'WPA3 PSK';
      case WifiAuthMode.WPA2_WPA3_PSK:
        return 'WPA2/WPA3 PSK';
      case WifiAuthMode.WAPI_PSK:
        return 'WAPI PSK';
      case WifiAuthMode.UNKNOWN:
      default:
        return 'Unknown';
    }
  }

  $: rows = group
    ? [
        { key: 'SSID', value: group.ssid },
        { key: 'Security', value: GetWifiAuthModeString(group.security) },
        { key: 'Saved', value: group.saved.toString() },
      ]
    : [];

  let password: string | null = null;
  $: validPassword = password && password.length > 0 && password.length <= 63;
  let showPasswordPrompt = false;

  function ConnectWiFi() {
    if (!group) return;

    const groupSsid = group.ssid;

    if (group.saved) {
      const data = SerializeWifiNetworkConnectCommand(groupSsid);
      WebSocketClient.Instance.Send(data);
      modalStore.close();
      return;
    }

    if (group.security === WifiAuthMode.Open) {
      password = null;
    } else if (!validPassword) {
      showPasswordPrompt = true;
      return;
    }

    const data = SerializeWifiNetworkSaveCommand(groupSsid, password, true);
    WebSocketClient.Instance.Send(data);
    modalStore.close();
  }
  function ForgetWiFi() {
    if (!group) return;
    const data = SerializeWifiNetworkForgetCommand(group.ssid);
    WebSocketClient.Instance.Send(data);
    modalStore.close();
  }
</script>

<div class="card p-4 w-[24rem] flex-col space-y-4">
  {#if group}
    <div class="flex justify-between space-x-2">
      <h2 class="h2">Network Info</h2>
      <button class="btn-icon variant-outline" on:click={() => modalStore.close()}><i class="fa fa-xmark"></i></button>
    </div>
    <div>
      {#each rows as row (row.key)}
        <span class="flex justify-between"><span class="font-bold">{row.key}:</span><span class="text-gray-700 dark:text-gray-300">{row.value}</span></span>
      {/each}
    </div>
    <!-- Per-AP info -->
    <div>
      <h3 class="h3">Access Points</h3>
      <!-- Scrollable list of APs -->
      <div class="flex flex-col space-y-2 p-2 max-h-64 overflow-y-auto">
        {#each group.networks as network}
          <div class="card p-2 flex justify-between items-center">
            <span class="font-bold">{network.bssid}</span>
            <span class="text-gray-700 dark:text-gray-300">{network.rssi} dBm</span>
            <span class="text-gray-700 dark:text-gray-300">Channel {network.channel}</span>
          </div>
        {/each}
      </div>
    </div>
    {#if showPasswordPrompt}
      <label class="label">
        <span>Password for {group.ssid}</span>
        <input class="input" type="password" bind:value={password} />
      </label>
    {/if}
    <div class="flex justify-end space-x-2">
      <div class="btn-group variant-outline">
        {#if showPasswordPrompt}
          <button on:click={() => (showPasswordPrompt = false)}>Cancel</button>
        {/if}
        <button on:click={ConnectWiFi} disabled={showPasswordPrompt && !validPassword}><i class={'fa mr-2 text-green-500' + (group.saved ? ' fa-wifi' : ' fa-link')}></i>Connect</button>
        {#if group.saved}
          <button on:click={ForgetWiFi}><i class="fa fa-trash mr-2 text-red-500"></i>Forget</button>
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
