<script lang="ts">
  import { linkAccount, unlinkAccount } from '$lib/api';
  import { hubState } from '$lib/stores';
  import { Button } from '$lib/components/ui/button';
  import { Input } from '$lib/components/ui/input';
  import { Label } from '$lib/components/ui/label';
  import { CircleCheck, Wifi, WifiOff, Unlink } from '@lucide/svelte';

  function isValidLinkCode(str: string) {
    if (typeof str != 'string') return false;
    for (let i = 0; i < str.length; i++) {
      if (str[i] < '0' || str[i] > '9') return false;
    }
    return true;
  }

  let linkCode: string = $state('');
  let linkCodeValid = $derived(isValidLinkCode(linkCode));
  let accountLinked = $derived(hubState.accountLinked);
  let wifiConnected = $derived(hubState.wifiConnectedBSSID !== null);
  let showRelink = $state(false);

  async function handleLinkAccount() {
    if (!linkCodeValid) return;
    const success = await linkAccount(linkCode!);
    if (success) showRelink = false;
  }

  async function handleUnlink() {
    await unlinkAccount();
    showRelink = true;
  }
</script>

<div class="flex flex-col gap-4">
  <div>
    <h3 class="text-lg font-semibold">Account Linking</h3>
    <p class="text-muted-foreground text-sm">
      Link this device to your OpenShock account to control it remotely.
    </p>
  </div>

  <!-- WiFi status -->
  {#if wifiConnected}
    <div class="flex items-center gap-2 rounded-lg border border-green-500/30 bg-green-500/10 p-3">
      <Wifi class="h-4 w-4 shrink-0 text-green-500" />
      <p class="text-sm text-green-700 dark:text-green-300">WiFi connected</p>
    </div>
  {:else}
    <div class="flex items-center gap-2 rounded-lg border border-yellow-500/30 bg-yellow-500/10 p-3">
      <WifiOff class="h-4 w-4 shrink-0 text-yellow-500" />
      <p class="text-sm text-yellow-700 dark:text-yellow-300">WiFi not connected</p>
    </div>
  {/if}

  <!-- Account status -->
  {#if accountLinked && !showRelink}
    <div class="flex items-center justify-between rounded-lg border border-green-500/30 bg-green-500/10 p-4">
      <div class="flex items-center gap-2">
        <CircleCheck class="h-5 w-5 text-green-500" />
        <p class="text-sm font-medium text-green-700 dark:text-green-300">Account linked</p>
      </div>
      <Button variant="ghost" size="sm" onclick={handleUnlink} title="Unlink and re-link">
        <Unlink class="mr-1.5 h-4 w-4" />
        Re-link
      </Button>
    </div>
  {:else}
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
        <Button onclick={handleLinkAccount} disabled={!linkCodeValid || linkCode.length < 6}>Link</Button>
      </div>
    </div>
  {/if}
</div>
