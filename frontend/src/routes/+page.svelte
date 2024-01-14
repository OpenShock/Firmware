<script lang="ts">
  import { browser } from '$app/environment';
  import { SerializeAccountLinkCommand } from '$lib/Serializers/AccountLinkCommand';
  import { SerializeSetRfTxPinCommand } from '$lib/Serializers/SetRfTxPinCommand';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import WiFiList from '$lib/components/WiFiList.svelte';
  import WiFiRefreshButton from '$lib/components/WiFiRefreshButton.svelte';
  import { DeviceStateStore } from '$lib/stores';
  import { Step, Stepper } from '@skeletonlabs/skeleton';

  function isValidLinkCode(str: string) {
    if (typeof str != 'string') return false;

    for (var i = 0; i < str.length; i++) {
      if (str[i] < '0' || str[i] > '9') return false;
    }

    return true;
  }

  let linkCode: string = '';
  $: linkCodeValid = isValidLinkCode(linkCode);

  let rfTxPin: number | null = $DeviceStateStore.config?.rf.txPin ?? null;
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

<Stepper class="p-2 sm:p-20 max-w-6xl mx-auto">
  <Step>
    <svelte:fragment slot="header">
      <div class="flex flex-row items-center">
        Connect to WiFi
        <div class="flex-grow" />
        <WiFiRefreshButton />
      </div>
    </svelte:fragment>
    <WiFiList />
  </Step>
  <Step class="h-full">
    <svelte:fragment slot="header">Link Account</svelte:fragment>

    <div class="flex flex-col space-y-2">
      <h3 class="h3">Account Linking</h3>
      <div class="flex space-x-2">
        <input class={'input variant-form-material ' + (linkCodeValid ? '' : 'input-error')} type="text" inputmode="numeric" pattern="[0-9]*" placeholder="Link Code" bind:value={linkCode} />
        <button class="btn variant-filled" on:click={linkAccount} disabled={!linkCodeValid || linkCode.length < 6}>Link</button>
      </div>
    </div>
  </Step>
  <!-- ... -->
</Stepper>
