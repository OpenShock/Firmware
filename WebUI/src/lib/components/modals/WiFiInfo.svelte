<script lang="ts">
	import type { WiFiNetwork } from "$lib/types/WiFiNetwork";
	import { getModalStore } from "@skeletonlabs/skeleton";

  export let item: WiFiNetwork;

  const modalStore = getModalStore();

  const rows = [
    { key: 'Name', value: item.name },
    { key: 'Security', value: item.security ?? 'None' },
    { key: 'Saved', value: item.saved },
  ]
</script>

<div class="card p-4 w-[24rem] flex-col space-y-4">
  <div class="flex justify-between space-x-2">
    <h2 class="h2">WiFi Info</h2>
    <button class="btn-icon variant-outline" on:click={() => modalStore.close()}><i class="fas fa-times"></i></button>
  </div>
  <div>
    {#each rows as row (row.key)}
      <span class="flex justify-between"><span class="font-bold">{row.key}:</span><span class="text-gray-300">{row.value}</span></span>
    {/each}
  </div>
  <div class="flex justify-end space-x-2">
    <div class="btn-group variant-outline">
      {#if item.saved}
        <button  on:click={() => modalStore.close()}><i class="fas fa-wifi mr-2 text-green-500"></i>Connect</button>
        <button on:click={() => modalStore.close()}><i class="fas fa-trash mr-2 text-red-500"></i>Forget</button>
      {:else}
        <button on:click={() => modalStore.close()}><i class="fas fa-link mr-2 text-green-500"></i>Connect</button>
      {/if}
    </div>
  </div>
</div>
