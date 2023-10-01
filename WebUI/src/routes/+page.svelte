<script lang="ts">
	import { WebSocketClient } from "$lib/WebSocketClient";
	import WiFiList from "$lib/components/WiFiList.svelte";

</script>

<div class="flex flex-col items-center justify-center h-full">
	<div class="flex-col space-y-5 w-full max-w-md">
    <WiFiList />
    <div>
      <h3 class="h3 mb-2">Other settings</h3>
      <div class="flex space-x-2">
        <input class="input variant-form-material" type="text" placeholder="TX Pin" />
        <button class="btn variant-filled">Configure</button>
      </div>
    </div>
  </div>
</div>
