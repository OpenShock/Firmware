<script lang="ts">
  import { SerializeGatewayPairCommand } from '$lib/Serializers/GatewayPairCommand';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import WiFiList from '$lib/components/WiFiList.svelte';

  function isValidPairCode(str: string) {
    if (typeof str != 'string') return false;

    for (var i = 0; i < str.length; i++) {
      if (str[i] < '0' || str[i] > '9') return false;
    }

    return true;
  }

  let pairCode: string = '';
  $: pairCodeValid = isValidPairCode(pairCode);

  function pair() {
    const data = SerializeGatewayPairCommand(pairCode);
    WebSocketClient.Instance.Send(data);
  }
</script>

<div class="flex flex-col items-center justify-center h-full">
  <div class="flex-col space-y-5 w-full max-w-md">
    <WiFiList />
    <div class="flex flex-col space-y-2">
      <h3 class="h3">Other settings</h3>
      <div class="flex space-x-2">
        <input class={'input variant-form-material ' + (pairCodeValid ? '' : 'input-error')} type="text" placeholder="Pair Code" bind:value={pairCode} />
        <button class="btn variant-filled" on:click={pair} disabled={!pairCodeValid || pairCode.length < 4}>Pair</button>
      </div>
      <div class="flex space-x-2">
        <input class="input variant-form-material" type="text" placeholder="TX Pin" />
        <button class="btn variant-filled">Configure</button>
      </div>
    </div>
  </div>
</div>
