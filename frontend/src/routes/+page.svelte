<script lang="ts">
  import { SerializeAccountLinkCommand } from '$lib/Serializers/AccountLinkCommand';
  import { SerializeSetRfTxPinCommand } from '$lib/Serializers/SetRfTxPinCommand';
  import { SerializeSetEstopPinCommand } from '$lib/Serializers/SetEstopPinCommand';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import GpioPinSelector from '$lib/components/GpioPinSelector.svelte';
  import WiFiList from '$lib/components/WiFiList.svelte';
  import { DeviceStateStore } from '$lib/stores';

  function isValidLinkCode(str: string) {
    if (typeof str != 'string') return false;

    for (var i = 0; i < str.length; i++) {
      if (str[i] < '0' || str[i] > '9') return false;
    }

    return true;
  }

  let linkCode: string = $state('');
  let linkCodeValid = $derived(isValidLinkCode(linkCode));

  function linkAccount() {
    if (!linkCodeValid) return;
    const data = SerializeAccountLinkCommand(linkCode!);
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
        <button class="btn variant-filled" onclick={linkAccount} disabled={!linkCodeValid || linkCode.length < 6}>Link</button>
      </div>
    </div>

    <GpioPinSelector name="RF TX Pin" currentPin={$DeviceStateStore.config?.rf?.txPin ?? null} serializer={SerializeSetRfTxPinCommand} />

    <!-- TODO: Add EStop Enable/Disable toggle -->

    <GpioPinSelector name="EStop Pin" currentPin={$DeviceStateStore.config?.estop?.gpioPin ?? null} serializer={SerializeSetEstopPinCommand} />
  </div>
</div>
