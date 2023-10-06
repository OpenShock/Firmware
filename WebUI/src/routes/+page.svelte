<script lang="ts">
  import { WebSocketClient } from '$lib/WebSocketClient';
  import WiFiList from '$lib/components/WiFiList.svelte';

  function pair() {
    WebSocketClient.Instance.Send('{ "type": "pair", "code": "123456" }');
  }
</script>

<div class="flex flex-col items-center justify-center h-full">
  <div class="flex-col space-y-5 w-full max-w-md">
    <WiFiList />
    <div class="flex flex-col space-y-2">
      <h3 class="h3">Other settings</h3>
      <div class="flex space-x-2">
        <input class="input variant-form-material" type="text" placeholder="Pair Code" />
        <button class="btn variant-filled" on:click={pair}>Pair</button>
      </div>
      <div class="flex space-x-2">
        <input class="input variant-form-material" type="text" placeholder="TX Pin" />
        <button class="btn variant-filled">Configure</button>
      </div>
    </div>
  </div>
</div>
