<script lang="ts">
  import { SerializeSetRfTxPinCommand } from '$lib/Serializers/SetRfTxPinCommand';
  import { DeviceStateStore } from '$lib/stores';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { ListBox, ListBoxItem } from '@skeletonlabs/skeleton';

  type GPIOConfig = {
    pin: number;
    input: boolean;
    output: boolean;
  };

  $: inputs = [0, 1, 2, 3, 15];
  $: outputs = [2, 3, 16];

  let gpios: GPIOConfig[] = [];
  $: {
    gpios = [];
    for (let i = 0; i < 256; i++) {
      const isInput = inputs.includes(i);
      const isOutput = outputs.includes(i);
      if (isInput || isOutput) {
        gpios.push({
          pin: i,
          input: isInput,
          output: isOutput,
        });
      }
    }
  }
</script>

<div class="flex flex-col space-y-2">
  <div class="flex flex-row space-x-2 items-center">
    <h3 class="h3">GPIO Configuration</h3>
  </div>
  <div class="table-container">
    <table class="table table-hover">
      <thead>
        <tr>
          <th>GPIO</th>
          <th>Type</th>
          <th>Functionality</th>
        </tr>
      </thead>
      <tbody>
        {#each gpios as gpio, i}
          <tr>
            <td>{gpio.pin}</td>
            <td>{gpio.input ? (gpio.output ? 'Input/Output' : 'Input') : 'Output'}</td>
            <td>
              Not implemented
              <button class="btn variant-filled">Set</button>
            </td>
          </tr>
        {/each}
      </tbody>
      <!--
      <tfoot>
        <tr>
          <th colspan="3">Summary example</th>
          <td>{'Summary value'}</td>
        </tr>
      </tfoot>
      -->
    </table>
  </div>
</div>
