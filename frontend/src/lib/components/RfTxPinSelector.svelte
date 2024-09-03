<script lang="ts">
  import { SerializeSetRfTxPinCommand } from '$lib/Serializers/SetRfTxPinCommand';
  import { DeviceStateStore } from '$lib/stores';
  import { WebSocketClient } from '$lib/WebSocketClient';

  let lastPin: number | null = null;
  let gpioPin: number | null = null;
  let presetText: string = 'Loading...';
  let validGpioPin: boolean = false;

  $: if ($DeviceStateStore.config !== null) {
    let pin = $DeviceStateStore.config.rf.txPin;

    if (pin !== lastPin) {
      gpioPin = pin;
      lastPin = pin;
    }

    if (gpioPin !== null) {
      presetText = 'Currently ' + gpioPin;
      validGpioPin = gpioPin >= 0 && gpioPin <= 255 && $DeviceStateStore.gpioValidOutputs.includes(gpioPin);
    } else {
      presetText = 'Not set';
      validGpioPin = false;
    }
  }

  function setGpioPin() {
    if (!validGpioPin) return;
    const data = SerializeSetRfTxPinCommand(gpioPin!);
    WebSocketClient.Instance.Send(data);
  }
</script>

<div class="flex flex-col space-y-2">
  <div class="flex flex-row space-x-2 items-center">
    <h3 class="h3">RF TX Pin</h3>
    <span class="text-sm text-gray-500">{presetText}</span>
  </div>
  <div class="flex space-x-2">
    <input class="input variant-form-material" type="number" placeholder="TX Pin" bind:value={gpioPin} />
    <button class="btn variant-filled" on:click={setGpioPin} disabled={!validGpioPin}>Set</button>
  </div>
</div>
