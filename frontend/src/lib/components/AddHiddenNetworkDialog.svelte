<script lang="ts">
  import { saveWifiNetwork } from '$lib/api';
  import { Button, buttonVariants } from '$lib/components/ui/button';
  import {
    Dialog,
    DialogContent,
    DialogDescription,
    DialogFooter,
    DialogHeader,
    DialogTitle,
    DialogTrigger,
  } from '$lib/components/ui/dialog';
  import { Input } from '$lib/components/ui/input';
  import { Label } from '$lib/components/ui/label';
  import { Plus } from '@lucide/svelte';

  const securityOptions = [
    { value: 0, label: 'Open (no password)' },
    { value: 2, label: 'WPA' },
    { value: 3, label: 'WPA2' },
    { value: 4, label: 'WPA/WPA2' },
    { value: 6, label: 'WPA3' },
    { value: 7, label: 'WPA2/WPA3' },
  ];

  let dialogOpen = $state(false);
  let ssid = $state('');
  let password = $state('');
  let security = $state(3); // WPA2 default

  let isOpen = $derived(security === 0);
  let needsPassword = $derived(!isOpen);
  let canSave = $derived(ssid.length > 0 && ssid.length <= 31 && (isOpen || password.length >= 8));

  function handleSave() {
    if (!canSave) return;
    saveWifiNetwork(ssid, isOpen ? null : password, true, security);
    dialogOpen = false;
    ssid = '';
    password = '';
    security = 3;
  }

  function handleOpenChange(open: boolean) {
    dialogOpen = open;
    if (!open) {
      ssid = '';
      password = '';
      security = 3;
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
      <DialogTitle>Add Hidden Network</DialogTitle>
      <DialogDescription>
        Enter the details for a hidden WiFi network
      </DialogDescription>
    </DialogHeader>
    <div class="flex flex-col gap-4 py-4">
      <div class="flex flex-row items-center gap-4">
        <Label for="hidden-ssid" class="w-20 text-right">SSID</Label>
        <Input id="hidden-ssid" class="flex-1" placeholder="Network name" bind:value={ssid} />
      </div>
      <div class="flex flex-row items-center gap-4">
        <Label for="hidden-security" class="w-20 text-right">Security</Label>
        <select
          id="hidden-security"
          class="border-input bg-background ring-offset-background placeholder:text-muted-foreground focus-visible:ring-ring flex h-10 w-full flex-1 rounded-md border px-3 py-2 text-sm focus-visible:ring-2 focus-visible:ring-offset-2 focus-visible:outline-none"
          bind:value={security}
        >
          {#each securityOptions as opt}
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
      <Button type="button" onclick={handleSave} disabled={!canSave}>Save & Connect</Button>
    </DialogFooter>
  </DialogContent>
</Dialog>
