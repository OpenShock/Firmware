<script lang="ts">
  import { WifiAuthMode } from '#lib/_fbs/open-shock/serialization/types/wifi-auth-mode.js';
  import { saveWifiNetwork } from '#lib/api.js';
  import { isValidWifiPassword, isValidWifiSsid } from '#lib/utils/wifiValidation.js';
  import { Button, buttonVariants } from '@openshock/svelte-core/components/ui/button';
  import {
    Dialog,
    DialogContent,
    DialogDescription,
    DialogFooter,
    DialogHeader,
    DialogTitle,
    DialogTrigger,
  } from '@openshock/svelte-core/components/ui/dialog';
  import { Input } from '@openshock/svelte-core/components/ui/input';
  import { Label } from '@openshock/svelte-core/components/ui/label';
  import { Plus } from '@lucide/svelte';

  const securityOptions = [
    { value: WifiAuthMode.Open, label: 'Open (no password)' },
    { value: WifiAuthMode.WPA_PSK, label: 'WPA' },
    { value: WifiAuthMode.WPA2_PSK, label: 'WPA2' },
    { value: WifiAuthMode.WPA_WPA2_PSK, label: 'WPA/WPA2' },
    { value: WifiAuthMode.WPA3_PSK, label: 'WPA3' },
    { value: WifiAuthMode.WPA2_WPA3_PSK, label: 'WPA2/WPA3' },
  ];

  let dialogOpen = $state(false);
  let saving = $state(false);
  let ssid = $state('');
  let password = $state('');
  let security = $state<WifiAuthMode>(WifiAuthMode.WPA2_PSK);

  let isOpen = $derived(security === WifiAuthMode.Open);
  let needsPassword = $derived(!isOpen);
  let canSave = $derived(isValidWifiSsid(ssid) && (isOpen || isValidWifiPassword(password)));

  async function handleSave(e: SubmitEvent) {
    e.preventDefault();
    if (!canSave || saving) return;
    saving = true;
    const saved = await saveWifiNetwork(ssid, isOpen ? null : password, true, security);
    saving = false;
    if (saved) handleOpenChange(false);
  }

  function handleOpenChange(open: boolean) {
    dialogOpen = open;
    if (!open) {
      ssid = '';
      password = '';
      security = WifiAuthMode.WPA2_PSK;
    }
  }
</script>

<Dialog bind:open={() => dialogOpen, handleOpenChange}>
  <DialogTrigger class={buttonVariants({ variant: 'outline', size: 'sm' })}>
    <Plus class="mr-1 h-4 w-4" />
    Hidden Network
  </DialogTrigger>
  <DialogContent class="sm:max-w-[425px]">
    <DialogHeader>
      <DialogTitle>Add hidden network</DialogTitle>
      <DialogDescription>Enter the details for a hidden WiFi network.</DialogDescription>
    </DialogHeader>
    <form class="contents" onsubmit={handleSave}>
      <div class="flex flex-col gap-4 py-4">
        <div class="flex flex-row items-center gap-4">
          <Label for="hidden-ssid" class="w-20 text-right">SSID</Label>
          <Input id="hidden-ssid" class="flex-1" placeholder="Network name" bind:value={ssid} />
        </div>
        <div class="flex flex-row items-center gap-4">
          <Label for="hidden-security" class="w-20 text-right">Security</Label>
          <select
            id="hidden-security"
            class="border-input focus-visible:border-ring focus-visible:ring-ring/50 dark:bg-input/30 h-8 w-full flex-1 rounded-lg border bg-transparent px-2.5 text-sm outline-none focus-visible:ring-3"
            bind:value={security}
          >
            {#each securityOptions as opt (opt.value)}
              <option value={opt.value}>{opt.label}</option>
            {/each}
          </select>
        </div>
        {#if needsPassword}
          <div class="flex flex-row items-center gap-4">
            <Label for="hidden-password" class="w-20 text-right">Password</Label>
            <Input
              id="hidden-password"
              class="flex-1"
              type="password"
              placeholder="Minimum 8 characters"
              bind:value={password}
            />
          </div>
        {/if}
      </div>
      <DialogFooter>
        <Button variant="outline" onclick={() => handleOpenChange(false)}>Cancel</Button>
        <Button type="submit" disabled={!canSave || saving}>Save & Connect</Button>
      </DialogFooter>
    </form>
  </DialogContent>
</Dialog>
