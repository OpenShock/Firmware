<script lang="ts">
	import { getModalStore } from '@skeletonlabs/skeleton';
	import WiFiInfo from '$lib/components/modals/WiFiInfo.svelte';
	import type { WiFiNetwork } from '$lib/types/WiFiNetwork';
	import { WiFiStateStore } from '$lib/stores';
	import { WebSocketClient } from '$lib/WebSocketClient';

  const modalStore = getModalStore();

  let connectedIndex = -1;

  function wifiScan() {
    WebSocketClient.Instance.Send('{ "type": "startScan" }');
  }
  function wifiPair(item: WiFiNetwork) {
    /*
    modalStore.trigger({
      type: 'prompt',
      title: 'Enter password',
      body: 'Enter the password for the network',
      value: '',
      valueAttr: { type: 'password', minlength: 1, maxlength: 32, required: true },
      response: (r: string) => {
        let index = items.findIndex(i => i.id === item.id);
        items[index].saved = true;
      }
    });
    */
  }
  function wifiConnect(item: WiFiNetwork) {
    console.log("wifiConnect", item);
    connectedIndex = item.index;
  }
  function wifiDisconnect(item: WiFiNetwork) {
    if (connectedIndex !== item.index) return;
    console.log("wifiDisconnect", item);
    connectedIndex = -1;
  }
  function wifiSettings(item: WiFiNetwork) {
    modalStore.trigger({
      type: 'component',
      component: {
        ref: WiFiInfo,
        props: { bssid: item.bssid },
        slot: '<p>Skeleton</p>'
      },
    });
  }
</script>

<div>
  <div class="flex justify-between items-center mb-2">
	  <h3 class="h3">Configure WiFi</h3>
    <button class="btn variant-outline" on:click={wifiScan} disabled={$WiFiStateStore.scanning}>
      {#if $WiFiStateStore.scanning}
        <i class="fa fa-spinner fa-spin"></i>
      {:else}
        <i class="fa fa-rotate-right"></i>
      {/if}
    </button>
  </div>
  <div class="max-h-64 overflow-auto">
    {#each $WiFiStateStore.networks as item (item.index)}
      <div class="card mb-2 p-2 flex justify-between items-center">
        <span>
          {#if item.index === connectedIndex}
            <i class="fa fa-wifi text-green-500"/>
          {:else}
            <i class="fa fa-wifi"/>
          {/if}
          {item.ssid}
        </span>
        <div class="btn-group variant-outline">
          {#if item.saved}
            <button on:click={() => wifiConnect(item)}><i class="fa fa-arrow-right text-green-500"/></button>
          {:else}
            <button on:click={() => wifiPair(item)}><i class="fa fa-link text-green-500"/></button>
          {/if}
          <button on:click={() => wifiSettings(item)}><i class="fa fa-cog"/></button>
        </div>
      </div>
    {/each}
  </div>
</div>
