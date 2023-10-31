<script lang="ts">
  import { SerializeGatewayPairCommand } from '$lib/Serializers/GatewayPairCommand';
  import { SerializeSetRfTxPinCommand } from '$lib/Serializers/SetRfTxPinCommand';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import WiFiList from '$lib/components/WiFiList.svelte';
  import { DeviceStateStore } from '$lib/stores';

  function isValidPairCode(str: string) {
    if (typeof str != 'string') return false;

    for (var i = 0; i < str.length; i++) {
      if (str[i] < '0' || str[i] > '9') return false;
    }

    return true;
  }

  let initialized = false;

  let pairCode: string = '';
  $: pairCodeValid = isValidPairCode(pairCode);

  let rfTxPin: number | null = $DeviceStateStore.rfTxPin;
  $: rfTxPinValid = rfTxPin !== null && rfTxPin >= 0 && rfTxPin < 255;

  $: if (!initialized) {
    rfTxPin = $DeviceStateStore.rfTxPin;
    initialized = true;
  }

  function pair() {
    if (!pairCodeValid) return;
    const data = SerializeGatewayPairCommand(pairCode!);
    WebSocketClient.Instance.Send(data);
  }

  function setRfTxPin() {
    if (!rfTxPinValid) return;
    const data = SerializeSetRfTxPinCommand(rfTxPin!);
    WebSocketClient.Instance.Send(data);
  }
</script>

<div class="flex flex-col items-center justify-center h-full">
  <div class="flex-col space-y-5 w-full max-w-md">
    <WiFiList />

    <div class="flex flex-col space-y-2">
      <h3 class="h3">Pairing</h3>
      <div class="flex space-x-2">
        <input class={'input variant-form-material ' + (pairCodeValid ? '' : 'input-error')} type="text" placeholder="Pair Code" bind:value={pairCode} />
        <button class="btn variant-filled" on:click={pair} disabled={!pairCodeValid || pairCode.length < 4}>Pair</button>
      </div>
    </div>

    <div class="flex flex-col space-y-2">
      <h3 class="h3">RF TX Pin</h3>
      <div class="flex space-x-2">
        <input class="input variant-form-material" type="number" placeholder="TX Pin" bind:value={rfTxPin} />
        <button class="btn variant-filled" on:click={setRfTxPin} disabled={!rfTxPinValid}>Set</button>
      </div>
    </div>
  </div>
</div>
