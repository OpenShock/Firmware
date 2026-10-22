<script lang="ts">
  import { setRfTxPin } from '#lib/api.js';
  import GpioPinSelector from '#lib/components/GpioPinSelector.svelte';
  import SectionHeader from '#lib/components/SectionHeader.svelte';
  import { hubState, usedPins } from '#lib/stores/index.js';

  // Shown but not configurable: the portal is unauthenticated, so the E-Stop is set up over the serial console only
  let estop = $derived(hubState.config?.estop);
  let estopHasPin = $derived(estop !== undefined && estop.gpioPin >= 0);

  // The hub refuses an RF TX pin that is the E-Stop's; mark it so the picker says so before trying
  $effect(() => {
    if (estop && estopHasPin) {
      usedPins.markPinUsed(estop.gpioPin, 'EStop');
    }
  });
</script>

<div class="flex flex-col gap-4">
  <SectionHeader
    title="Hardware Configuration"
    description="Configure the GPIO pins for your hardware. Default values are provided for most boards."
  />

  <div class="grid gap-4 md:grid-cols-2">
    <div class="rounded-lg border p-4">
      <p class="text-muted-foreground mb-3 text-xs">
        The RF transmitter pin controls the 433 MHz radio used to communicate with shockers.
      </p>
      <GpioPinSelector
        name="RF TX Pin"
        currentPin={hubState.config?.rf?.txPin ?? null}
        validPins={hubState.gpioValidOutputs}
        setter={setRfTxPin}
      />
    </div>

    <div class="rounded-lg border p-4">
      <p class="text-muted-foreground mb-3 text-xs">
        The emergency stop pin provides a hardware kill switch for all shocker output.
      </p>
      <dl class="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1 text-sm">
        <dt class="text-muted-foreground">Status</dt>
        <dd>{estop ? (estop.enabled ? 'Enabled' : 'Disabled') : 'Loading...'}</dd>
        <dt class="text-muted-foreground">Pin</dt>
        <dd>{estop ? (estopHasPin ? estop.gpioPin : 'None') : 'Loading...'}</dd>
      </dl>
      <p class="text-muted-foreground mt-3 text-xs">
        The E-Stop can only be configured over the serial console.
      </p>
    </div>
  </div>
</div>
