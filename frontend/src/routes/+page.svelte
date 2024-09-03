<script lang="ts">
  import { SerializeAccountLinkCommand } from '$lib/Serializers/AccountLinkCommand';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import RfTxPinSelector from '$lib/components/RfTxPinSelector.svelte';
  import WiFiList from '$lib/components/WiFiList.svelte';

  function isValidLinkCode(str: string) {
    if (typeof str != 'string') return false;

    for (var i = 0; i < str.length; i++) {
      if (str[i] < '0' || str[i] > '9') return false;
    }

    return true;
  }

  let linkCode: string = '';
  $: linkCodeValid = isValidLinkCode(linkCode);

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
        <button class="btn variant-filled" on:click={linkAccount} disabled={!linkCodeValid || linkCode.length < 6}>Link</button>
      </div>
    </div>

    <RfTxPinSelector />
  </div>
</div>
