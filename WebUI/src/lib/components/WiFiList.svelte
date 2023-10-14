<script lang="ts">
  import { getModalStore } from '@skeletonlabs/skeleton';
  import WiFiInfo from '$lib/components/modals/WiFiDetails.svelte';
  import type { WiFiNetwork } from '$lib/types/WiFiNetwork';
  import { WiFiStateStore } from '$lib/stores';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { Builder as FlatbufferBuilder } from 'flatbuffers';
  import { WifiScanCommand } from '$lib/fbs/open-shock/serialization/local/wifi-scan-command';
  import { LocalToDeviceMessage } from '$lib/fbs/open-shock/serialization/local/local-to-device-message';
  import { LocalToDeviceMessagePayload } from '$lib/fbs/open-shock/serialization/local/local-to-device-message-payload';
  import { WifiAuthMode } from '$lib/fbs/open-shock/wifi-auth-mode';

  const modalStore = getModalStore();

  let connectedBSSID: string | null = null;

  function wifiScan() {
    const fbb = new FlatbufferBuilder(64);

    const cmdOffset = WifiScanCommand.createWifiScanCommand(fbb, !$WiFiStateStore.scanning);

    const payloadOffset = LocalToDeviceMessage.createLocalToDeviceMessage(fbb, LocalToDeviceMessagePayload.WifiScanCommand, cmdOffset);

    fbb.finish(payloadOffset);

    const buf = fbb.asUint8Array();

    WebSocketClient.Instance.Send(buf);
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
          WebSocketClient.Instance.Send(`{ "type": "wifi", "action": "authenticate", "bssid": "${item.bssid}", "password": "${password}" }`);
        },
      });
    } else {
      WebSocketClient.Instance.Send(`{ "type": "wifi", "action": "authenticate", "bssid": "${item.bssid}" }`);
    }
  }
  function wifiConnect(item: WiFiNetwork) {
    WebSocketClient.Instance.Send(`{ "type": "wifi", "action": "connect", "bssid": "${item.bssid}" }`);
  }
  function wifiDisconnect(item: WiFiNetwork) {
    WebSocketClient.Instance.Send(`{ "type": "wifi", "action": "disconnect", "bssid": "${item.bssid}" }`);
  }
  function wifiSettings(item: WiFiNetwork) {
    modalStore.trigger({
      type: 'component',
      component: {
        ref: WiFiInfo,
        props: { bssid: item.bssid },
      },
    });
  }
</script>

<div>
  <div class="flex justify-between items-center mb-2">
    <h3 class="h3">Configure WiFi</h3>
    <button class="btn variant-outline" on:click={wifiScan}>
      {#if $WiFiStateStore.scanning}
        <i class="fa fa-spinner fa-spin"></i>
      {:else}
        <i class="fa fa-rotate-right"></i>
      {/if}
    </button>
  </div>
  <div class="max-h-64 overflow-auto">
    {#each $WiFiStateStore.networks as [key, network] (key)}
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
