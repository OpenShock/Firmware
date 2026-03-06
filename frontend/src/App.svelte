<script lang="ts">
  import { SerializeAccountLinkCommand } from '$lib/Serializers/AccountLinkCommand';
  import { SerializeSetEstopPinCommand } from '$lib/Serializers/SetEstopPinCommand';
  import { SerializeSetRfTxPinCommand } from '$lib/Serializers/SetRfTxPinCommand';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import GpioPinSelector from '$lib/components/GpioPinSelector.svelte';
  import Footer from '$lib/components/Layout/Footer.svelte';
  import Header from '$lib/components/Layout/Header.svelte';
  import WiFiList from '$lib/components/WiFiList.svelte';
  import { Button } from '$lib/components/ui/button';
  import { Input } from '$lib/components/ui/input';
  import { Label } from '$lib/components/ui/label';
  import { Toaster } from '$lib/components/ui/sonner';
  import { HubStateStore, initializeDarkModeStore } from '$lib/stores';
  import { onMount } from 'svelte';

  onMount(() => {
    initializeDarkModeStore();
    WebSocketClient.Instance.Connect();
  });

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

<Toaster position="top-center" />

<div class="flex min-h-screen flex-col">
  <Header />

  <div class="flex flex-1 flex-col items-center justify-center px-2">
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
          <Button onclick={linkAccount} disabled={!linkCodeValid || linkCode.length < 6}>
            Link
          </Button>
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

  <Footer />
</div>
