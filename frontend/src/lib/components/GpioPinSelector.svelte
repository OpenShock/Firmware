<script lang="ts">
  import { DeviceStateStore } from '$lib/stores';
  import { WebSocketClient } from '$lib/WebSocketClient';

  export let name: string;
  export let currentPin: number | null;
  export let serializer: (gpio: number) => Uint8Array;

  function isValidPin(pin: number) {
    return pin >= 0 && pin <= 255 && $DeviceStateStore.gpioValidOutputs.includes(pin);
  }

  let pendingPin: number | null = null;

  $: statusText = currentPin === null ? 'Loading...' : currentPin >= 0 ? 'Currently ' + currentPin : 'Invalid pin';
  $: canSet = pendingPin !== null && pendingPin !== currentPin && isValidPin(pendingPin);

  $: if (pendingPin !== null) {
    if (pendingPin < 0) {
      pendingPin = null;
    } else if (pendingPin > 255) {
      pendingPin = 255;
    }
  }

  function setGpioPin() {
    if (!canSet) return;
    const data = serializer(currentPin!);
    WebSocketClient.Instance.Send(data);
  }
</script>

<div class="flex flex-col space-y-2">
  <div class="flex flex-row space-x-2 items-center">
    <h3 class="h3">{name}</h3>
    <span class="text-sm text-gray-500">{statusText}</span>
  </div>
  <div class="flex space-x-2">
    <input class="input variant-form-material" type="number" placeholder="GPIO Pin" bind:value={pendingPin} />
    <button class="btn variant-filled" on:click={setGpioPin} disabled={!canSet}>Set</button>
  </div>
</div>
