<script lang="ts">
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { Button } from '$lib/components/ui/button';
  import { Input } from '$lib/components/ui/input';
  import { HubStateStore, UsedPinsStore } from '$lib/stores';

  interface Props {
    name: string;
    currentPin: number | null;
    serializer: (gpio: number) => Uint8Array;
  }

  let { name, currentPin, serializer }: Props = $props();

  function isPinValid(pin: number | null): pin is number {
    return pin !== null && pin >= 0 && pin <= 255;
  }

  let currentPinValid = $derived(
    isPinValid(currentPin) && $HubStateStore.gpioValidOutputs.includes(currentPin)
  );

  let pendingPin = $state<number | null>(null);
  let pendingPinValid = $derived(
    isPinValid(pendingPin) && $HubStateStore.gpioValidOutputs.includes(pendingPin)
  );

  let statusText = $derived.by<string>(() => {
    if (pendingPin !== null) {
      if (!pendingPinValid) return 'Invalid pin';
      if ($UsedPinsStore.has(pendingPin)) return 'Pin already in use';
    }
    if (currentPin === null) return 'Loading...';
    if (!currentPinValid) return 'Invalid pin';

    return 'Currently ' + currentPin;
  });

  let canSet = $derived(
    pendingPin !== currentPin && pendingPinValid && !$UsedPinsStore.has(pendingPin!)
  );

  $effect(() => {
    if (pendingPin !== null) {
      if (pendingPin < 0) {
        pendingPin = null;
      } else if (pendingPin > 255) {
        pendingPin = 255;
      }
    }
  });

  $effect(() => {
    if (currentPin !== null) {
      UsedPinsStore.markPinUsed(currentPin, name);
    }
  });

  function setGpioPin() {
    if (!canSet) return;
    const data = serializer(pendingPin!);
    WebSocketClient.Instance.Send(data);
  }
</script>

<div class="flex flex-col space-y-2">
  <div class="flex flex-row items-center space-x-2">
    <h4 class="scroll-m-20 text-xl font-semibold tracking-tight">{name}</h4>
    <p class="text-sm text-muted-foreground">{statusText}</p>
  </div>
  <div class="flex space-x-2">
    <Input
      class="input variant-form-material"
      type="number"
      placeholder="GPIO Pin"
      bind:value={pendingPin}
    />
    <Button onclick={setGpioPin} disabled={!canSet}>Set</Button>
  </div>
</div>
