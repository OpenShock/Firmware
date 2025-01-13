<script lang="ts">
  import { HubStateStore } from '$lib/stores';
  import { UsedPinsStore } from '$lib/stores/UsedPinsStore';
  import { WebSocketClient } from '$lib/WebSocketClient';

  export let name: string;
  export let currentPin: number | null;
  export let serializer: (gpio: number) => Uint8Array;

  let pendingPin: number | null = null;
  let statusText: string = 'Loading...';

  $: canSet =
    pendingPin !== null &&
    pendingPin !== currentPin &&
    pendingPin >= 0 &&
    pendingPin <= 255 &&
    $HubStateStore.gpioValidOutputs.includes(pendingPin) &&
    !$UsedPinsStore.has(pendingPin);

  $: if (currentPin !== null) {
    UsedPinsStore.markPinUsed(currentPin, name);
    statusText = currentPin >= 0 ? 'Currently ' + currentPin : 'Invalid pin';
  } else {
    statusText = 'Loading...';
  }

  $: if (pendingPin !== null) {
    if (pendingPin < 0) {
      pendingPin = null;
    } else if (pendingPin > 255) {
      pendingPin = 255;
    }
  }

  function setGpioPin() {
    if (!canSet) return;
    const data = serializer(pendingPin!);
    WebSocketClient.Instance.Send(data);
  }
</script>

<div class="flex flex-col space-y-2">
  <div class="flex flex-row space-x-2 items-center">
    <h3 class="h3">{name}</h3>
    <span class="text-sm text-gray-500">{statusText}</span>
  </div>
  <div class="flex space-x-2">
    <input
      class="input variant-form-material"
      type="number"
      placeholder="GPIO Pin"
      bind:value={pendingPin}
    />
    <button class="btn variant-filled" on:click={setGpioPin} disabled={!canSet}>Set</button>
  </div>
</div>
