<script lang="ts">
  import { WiFiStateStore } from '$lib/stores';
  import { getModalStore } from '@skeletonlabs/skeleton';

  export let bssid: string;

  const modalStore = getModalStore();

  $: item = $WiFiStateStore.networks[bssid];

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
          <button on:click={() => modalStore.close()}><i class="fa fa-wifi mr-2 text-green-500"></i>Connect</button>
          <button on:click={() => modalStore.close()}><i class="fa fa-trash mr-2 text-red-500"></i>Forget</button>
        {:else}
          <button on:click={() => modalStore.close()}><i class="fa fa-link mr-2 text-green-500"></i>Connect</button>
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
