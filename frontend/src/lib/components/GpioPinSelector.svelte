<script lang="ts">
  import { usedPins } from '#lib/stores/index.js';
  import { Button } from '@openshock/svelte-core/components/ui/button';
  import { Input } from '@openshock/svelte-core/components/ui/input';

  interface Props {
    name: string;
    currentPin: number | null;
    // Pins the hub accepts for this role (outputs for RF TX, inputs for the E-Stop)
    validPins: Int8Array;
    setter: (pin: number) => Promise<boolean>;
  }

  let { name, currentPin, validPins, setter }: Props = $props();

  function isPinValid(pin: number | null): pin is number {
    return pin !== null && pin >= 0 && pin <= 255;
  }

  let currentPinValid = $derived(isPinValid(currentPin) && validPins.includes(currentPin));

  let pendingPin = $state<number | null>(null);
  let pendingPinValid = $derived(isPinValid(pendingPin) && validPins.includes(pendingPin));

  let statusText = $derived.by<string>(() => {
    if (pendingPin !== null) {
      if (!pendingPinValid) return 'Invalid pin';
      if (usedPins.has(pendingPin)) return 'Pin already in use';
    }
    if (currentPin === null) return 'Loading...';
    if (!currentPinValid) return 'Invalid pin';

    return 'Currently ' + currentPin;
  });

  let canSet = $derived(pendingPin !== currentPin && pendingPinValid && !usedPins.has(pendingPin!));

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
      usedPins.markPinUsed(currentPin, name);
    }
  });

  let saving = $state(false);

  async function setGpioPin(e: SubmitEvent) {
    e.preventDefault();
    if (!canSet || saving) return;
    saving = true;
    const success = await setter(pendingPin!);
    saving = false;
    if (success) {
      pendingPin = null;
    }
  }
</script>

<div class="flex flex-col gap-2">
  <div class="flex flex-row items-center gap-2">
    <h2 class="text-sm font-medium">{name}</h2>
    <p class="text-muted-foreground text-sm">{statusText}</p>
  </div>
  <form class="flex gap-2" onsubmit={setGpioPin}>
    <Input type="number" placeholder="GPIO Pin" bind:value={pendingPin} />
    <Button variant="outline" type="submit" disabled={!canSet || saving}>Set</Button>
  </form>
</div>
