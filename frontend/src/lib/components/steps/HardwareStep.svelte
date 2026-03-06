<script lang="ts">
  import { setRfTxPin, setEstopPin } from '$lib/api';
  import GpioPinSelector from '$lib/components/GpioPinSelector.svelte';
  import { hubState } from '$lib/stores';
</script>

<div class="flex flex-col gap-6">
  <div>
    <h3 class="text-lg font-semibold">Hardware Configuration</h3>
    <p class="text-muted-foreground text-sm">
      Configure the GPIO pins for your hardware. Default values are provided for most boards.
    </p>
  </div>

  <div class="flex flex-col gap-4">
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
      <!-- TODO: Add EStop Enable/Disable toggle -->
      <GpioPinSelector
        name="EStop Pin"
        currentPin={hubState.config?.estop?.gpioPin ?? null}
        setter={setEstopPin}
      />
    </div>
  </div>
</div>
