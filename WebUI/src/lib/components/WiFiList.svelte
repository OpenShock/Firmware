<script lang="ts">
  import { getModalStore } from '@skeletonlabs/skeleton';
  import WiFiInfo from '$lib/components/modals/WiFiDetails.svelte';
  import type { WiFiNetwork } from '$lib/types/WiFiNetwork';
  import { WiFiStateStore } from '$lib/stores';
  import { WebSocketClient } from '$lib/WebSocketClient';

  const modalStore = getModalStore();

  let connectedBSSID: string | null = null;

  function wifiScan() {
    if ($WiFiStateStore.scanning) {
      WebSocketClient.Instance.Send('{ "type": "wifi", "action": "scan", "run": false }');
    } else {
      WebSocketClient.Instance.Send('{ "type": "wifi", "action": "scan", "run": true }');
    }
  }
  function wifiAuthenticate(item: WiFiNetwork) {
    if (item.security !== 'Open') {
      modalStore.trigger({
        type: 'prompt',
        title: 'Enter password',
        body: 'Enter the password for the network',
        value: '',
        valueAttr: { type: 'password', minlength: 1, maxlength: 32, required: true },
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
    {#each Object.values($WiFiStateStore.networks) as item (item.bssid)}
      <div class="card mb-2 p-2 flex justify-between items-center">
        <span>
          {#if item.bssid === connectedBSSID}
            <i class="fa fa-wifi text-green-500" />
          {:else}
            <i class="fa fa-wifi" />
          {/if}
          {item.ssid}
        </span>
        <div class="btn-group variant-outline">
          {#if item.saved}
            <button on:click={() => wifiConnect(item)}><i class="fa fa-arrow-right text-green-500" /></button>
          {:else}
            <button on:click={() => wifiAuthenticate(item)}><i class="fa fa-link text-green-500" /></button>
          {/if}
          <button on:click={() => wifiSettings(item)}><i class="fa fa-cog" /></button>
        </div>
      </div>
    {/each}
  </div>
</div>
