<script lang="ts">
  import { SerializeAccountLinkCommand } from '$lib/Serializers/AccountLinkCommand';
  import { SerializeSetRfTxPinCommand } from '$lib/Serializers/SetRfTxPinCommand';
  import { SerializeSetEstopPinCommand } from '$lib/Serializers/SetEstopPinCommand';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import GpioPinSelector from '$lib/components/GpioPinSelector.svelte';
  import WiFiList from '$lib/components/WiFiList.svelte';
  import { Button } from '$lib/components/ui/button';
  import { Input } from '$lib/components/ui/input';
  import { Label } from '$lib/components/ui/label';
  import { HubStateStore } from '$lib/stores';

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

<div class="flex h-full flex-col items-center justify-center">
  <div class="w-full max-w-md flex-col space-y-5">
    <WiFiList />

    <div class="flex flex-col space-y-2">
      <Label for="account-link-code" class="scroll-m-20 text-xl font-semibold tracking-tight">
        Account Linking
      </Label>
      <div class="flex space-x-2">
        <Input
          class={linkCodeValid ? '' : 'input-error'}
          type="text"
          id="account-link-code"
          inputmode="numeric"
          pattern="[0-9]*"
          placeholder="Link Code"
          bind:value={linkCode}
        />
        <Button onclick={linkAccount} disabled={!linkCodeValid || linkCode.length < 6}>Link</Button>
      </div>
    </div>

    <GpioPinSelector
      name="RF TX Pin"
      currentPin={$HubStateStore.config?.rf?.txPin ?? null}
      serializer={SerializeSetRfTxPinCommand}
    />

    <!-- TODO: Add EStop Enable/Disable toggle -->

    <GpioPinSelector
      name="EStop Pin"
      currentPin={$HubStateStore.config?.estop?.gpioPin ?? null}
      serializer={SerializeSetEstopPinCommand}
    />
  </div>
</div>
