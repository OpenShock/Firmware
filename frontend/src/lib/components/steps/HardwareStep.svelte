<script lang="ts">
  import { setRfTxPin, setEstopPin, setEstopEnabled } from '#lib/api.js';
  import GpioPinSelector from '#lib/components/GpioPinSelector.svelte';
  import SectionHeader from '#lib/components/SectionHeader.svelte';
  import { hubState } from '#lib/stores/index.js';

  async function toggleEstop() {
    await setEstopEnabled(!(hubState.config?.estop?.enabled ?? false));
  }
</script>

<div class="flex flex-col gap-4">
  <SectionHeader
    title="Hardware Configuration"
    description="Configure the GPIO pins for your hardware. Default values are provided for most boards."
  />

  <div class="rounded-lg border p-4">
    <p class="text-muted-foreground mb-3 text-xs">
      The RF transmitter pin controls the 433 MHz radio used to communicate with shockers.
    </p>
    <GpioPinSelector
      name="RF TX Pin"
      currentPin={hubState.config?.rf?.txPin ?? null}
      setter={setRfTxPin}
    />
  </div>

  <div class="rounded-lg border p-4">
    <p class="text-muted-foreground mb-3 text-xs">
      The emergency stop pin provides a hardware kill switch for all shocker output.
    </p>
    <label class="mb-3 flex cursor-pointer items-center justify-between">
      <span class="text-sm font-medium">EStop Enabled</span>
      <input
        type="checkbox"
        checked={hubState.config?.estop?.enabled ?? false}
        onchange={toggleEstop}
        class="h-4 w-4"
      />
    </label>
    <GpioPinSelector
      name="EStop Pin"
      currentPin={hubState.config?.estop?.gpioPin ?? null}
      setter={setEstopPin}
    />
  </div>
</div>
