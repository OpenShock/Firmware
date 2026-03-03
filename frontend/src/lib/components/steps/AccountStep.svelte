<script lang="ts">
  import { SerializeAccountLinkCommand } from '$lib/Serializers/AccountLinkCommand';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { HubStateStore } from '$lib/stores';
  import { Button } from '$lib/components/ui/button';
  import { Input } from '$lib/components/ui/input';
  import { Label } from '$lib/components/ui/label';
  import { CircleCheck, TriangleAlert } from '@lucide/svelte';

  function isValidLinkCode(str: string) {
    if (typeof str != 'string') return false;
    for (let i = 0; i < str.length; i++) {
      if (str[i] < '0' || str[i] > '9') return false;
    }
    return true;
  }

  let linkCode: string = $state('');
  let linkCodeValid = $derived(isValidLinkCode(linkCode));
  let accountLinked = $derived($HubStateStore.accountLinked);

  function linkAccount() {
    if (!linkCodeValid) return;
    const data = SerializeAccountLinkCommand(linkCode!);
    WebSocketClient.Instance.Send(data);
  }
</script>

<div class="flex flex-col gap-4">
  <div>
    <h3 class="text-lg font-semibold">Account Linking</h3>
    <p class="text-muted-foreground text-sm">
      Link this device to your OpenShock account to control it remotely.
    </p>
  </div>

  {#if accountLinked}
    <div class="flex items-center gap-2 rounded-lg border border-green-500/30 bg-green-500/10 p-4">
      <CircleCheck class="h-5 w-5 text-green-500" />
      <p class="text-sm font-medium text-green-700 dark:text-green-300">
        Account linked successfully! The captive portal will close shortly.
      </p>
    </div>
  {:else}
    <div
      class="flex items-center gap-2 rounded-lg border border-yellow-500/30 bg-yellow-500/10 p-3"
    >
      <TriangleAlert class="h-5 w-5 shrink-0 text-yellow-500" />
      <p class="text-xs text-yellow-700 dark:text-yellow-300">
        After linking, the device will connect to the OpenShock gateway and this setup portal will
        close automatically.
      </p>
    </div>

    <div class="flex flex-col gap-2">
      <Label for="account-link-code">Link Code</Label>
      <p class="text-muted-foreground text-xs">
        Find your link code on the OpenShock website under device settings.
      </p>
      <div class="flex gap-2">
        <Input
          class={linkCodeValid ? '' : 'input-error'}
          type="text"
          id="account-link-code"
          inputmode="numeric"
          pattern="[0-9]*"
          placeholder="Enter link code"
          bind:value={linkCode}
        />
        <Button onclick={linkAccount} disabled={!linkCodeValid || linkCode.length < 6}>Link</Button>
      </div>
    </div>
  {/if}
</div>
