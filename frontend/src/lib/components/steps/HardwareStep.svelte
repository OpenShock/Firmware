<script lang="ts">
  import { setRfTxPin, setEstopPin, setEstopEnabled } from '#lib/api.js';
  import GpioPinSelector from '#lib/components/GpioPinSelector.svelte';
  import SectionHeader from '#lib/components/SectionHeader.svelte';
  import SettingSwitch from '#lib/components/SettingSwitch.svelte';
  import { hubState } from '#lib/stores/index.js';
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
        setter={setRfTxPin}
      />
    </div>

    <div class="rounded-lg border p-4">
      <p class="text-muted-foreground mb-3 text-xs">
        The emergency stop pin provides a hardware kill switch for all shocker output.
      </p>
      <SettingSwitch
        id="estop-enabled"
        class="mb-3"
        label="EStop Enabled"
        checked={hubState.config?.estop?.enabled ?? false}
        onchange={hubState.config ? setEstopEnabled : undefined}
      />
      <GpioPinSelector
        name="EStop Pin"
        currentPin={hubState.config?.estop?.gpioPin ?? null}
        setter={setEstopPin}
      />
    </div>
  </div>
</div>
