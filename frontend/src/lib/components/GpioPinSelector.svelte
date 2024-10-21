<script lang="ts">
  import { run } from 'svelte/legacy';

  import { DeviceStateStore } from '$lib/stores';
  import { UsedPinsStore } from '$lib/stores/UsedPinsStore';
  import { WebSocketClient } from '$lib/WebSocketClient';

  interface Props {
    name: string;
    currentPin: number | null;
    serializer: (gpio: number) => Uint8Array;
  }

  let { name, currentPin, serializer }: Props = $props();

  let pendingPin: number | null = $state(null);
  let statusText: string = $state('Loading...');

  let canSet = $derived(pendingPin !== null && pendingPin !== currentPin && pendingPin >= 0 && pendingPin <= 255 && $DeviceStateStore.gpioValidOutputs.includes(pendingPin) && !$UsedPinsStore.has(pendingPin));

  run(() => {
    if (currentPin !== null) {
      UsedPinsStore.markPinUsed(currentPin, name);
      statusText = currentPin >= 0 ? 'Currently ' + currentPin : 'Invalid pin';
    } else {
      statusText = 'Loading...';
    }
  });

  run(() => {
    if (pendingPin !== null) {
      if (pendingPin < 0) {
        pendingPin = null;
      } else if (pendingPin > 255) {
        pendingPin = 255;
      }
    }
  });

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
    <input class="input variant-form-material" type="number" placeholder="GPIO Pin" bind:value={pendingPin} />
    <button class="btn variant-filled" onclick={setGpioPin} disabled={!canSet}>Set</button>
  </div>
</div>
