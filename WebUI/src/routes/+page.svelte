<script lang="ts">
  import { browser } from '$app/environment';
  import { SerializeAccountLinkCommand } from '$lib/Serializers/AccountLinkCommand';
  import { SerializeSetRfTxPinCommand } from '$lib/Serializers/SetRfTxPinCommand';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import WiFiList from '$lib/components/WiFiList.svelte';
  import { DeviceStateStore } from '$lib/stores';

  function isValidLinkCode(str: string) {
    if (typeof str != 'string') return false;

    for (var i = 0; i < str.length; i++) {
      if (str[i] < '0' || str[i] > '9') return false;
    }

    return true;
  }

  let linkCode: string = '';
  $: linkCodeValid = isValidLinkCode(linkCode);

  let rfTxPin: number | null = $DeviceStateStore.rfTxPin;
  $: rfTxPinValid = rfTxPin !== null && rfTxPin >= 0 && rfTxPin < 255;

  function linkAccount() {
    if (!linkCodeValid) return;
    const data = SerializeAccountLinkCommand(linkCode!);
    WebSocketClient.Instance.Send(data);
  }

  function setRfTxPin() {
    if (!rfTxPinValid) return;
    const data = SerializeSetRfTxPinCommand(rfTxPin!);
    WebSocketClient.Instance.Send(data);
  }
</script>

<div class="flex flex-col items-center justify-center h-full">
  <div class="flex-col space-y-5 w-full max-w-md">
    <WiFiList />

    <div class="flex flex-col space-y-2">
      <h3 class="h3">Account Linking</h3>
      <div class="flex space-x-2">
        <input class={'input variant-form-material ' + (linkCodeValid ? '' : 'input-error')} type="text" inputmode="numeric" pattern="[0-9]*" placeholder="Link Code" bind:value={linkCode} />
        <button class="btn variant-filled" on:click={linkAccount} disabled={!linkCodeValid || linkCode.length < 6}>Pair</button>
      </div>
    </div>

    <div class="flex flex-col space-y-2">
      <div class="flex flex-row space-x-2 items-center">
        <h3 class="h3">RF TX Pin</h3>
        <span class="text-sm text-gray-500">(Currently {$DeviceStateStore.rfTxPin == null ? ' not set' : $DeviceStateStore.rfTxPin}) </span>
      </div>
      <div class="flex space-x-2">
        <input class="input variant-form-material" type="number" placeholder="TX Pin" bind:value={rfTxPin} />
        <button class="btn variant-filled" on:click={setRfTxPin} disabled={!rfTxPinValid}>Set</button>
      </div>
    </div>
  </div>
</div>
