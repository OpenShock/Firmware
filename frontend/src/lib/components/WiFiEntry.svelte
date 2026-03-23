<script lang="ts">
  import { hubState } from '$lib/stores';
  import { WifiAuthMode } from '$lib/_fbs/open-shock/serialization/types/wifi-auth-mode';
  import {
    forgetWifiNetwork,
    saveWifiNetwork,
    connectWifiNetwork,
    disconnectWifiNetwork,
  } from '$lib/api';
  import WiFiDetailsDialog from '$lib/components/WiFiDetailsDialog.svelte';
  import type { WiFiNetworkGroup } from '$lib/types';
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
  import {
    ArrowRight,
    Link,
    Wifi,
    WifiOff,
    Trash2,
    KeyRound,
    Signal,
    SignalLow,
    SignalMedium,
    SignalHigh,
  } from '@lucide/svelte';

  interface Props {
    ssid: string;
    netgroup?: WiFiNetworkGroup;
  }

  let { ssid, netgroup }: Props = $props();

  let isPresent = $derived(netgroup != null);
  let isSaved = $derived(netgroup?.saved ?? true);

  let connectedBSSID = $derived(hubState.wifiConnectedBSSID);
  let isConnected = $derived(
    netgroup != null && netgroup.networks.some((n) => n.bssid === connectedBSSID)
  );
  let bestRssi = $derived(netgroup?.networks[0]?.rssi ?? -100);

  let connectDialogOpen = $state(false);
  let editPasswordDialogOpen = $state(false);
  let pendingPassword = $state<string | null>(null);
  let editPassword = $state<string | null>(null);

  function wifiAuthenticate(password: string | null) {
    connectDialogOpen = false;
    saveWifiNetwork(ssid, password, true);
  }

  function wifiConnect() {
    connectWifiNetwork(ssid);
  }

  function wifiDisconnect() {
    disconnectWifiNetwork();
  }

  function wifiForget() {
    forgetWifiNetwork(ssid);
  }

  function wifiEditPassword(password: string | null) {
    editPasswordDialogOpen = false;
    saveWifiNetwork(ssid, password, false);
  }

  function handleConnectDialogOpenChange(open: boolean) {
    connectDialogOpen = open;
    if (!open) {
      pendingPassword = null;
    }
  }

  function handleEditPasswordDialogOpenChange(open: boolean) {
    editPasswordDialogOpen = open;
    if (!open) {
      editPassword = null;
    }
  }
</script>

<div
  class="mb-1 flex items-center justify-between rounded-md p-2 transition-colors {isConnected
    ? 'border border-green-500/30 bg-green-500/10'
    : 'hover:bg-muted/50'}"
>
  <div class="flex min-w-0 flex-1 items-center gap-2">
    {#if isConnected}
      <Wifi class="h-4 w-4 shrink-0 text-green-500" />
    {:else if !isPresent}
      <WifiOff class="text-muted-foreground h-4 w-4 shrink-0" />
    {:else if bestRssi > -50}
      <SignalHigh class="text-muted-foreground h-4 w-4 shrink-0" />
    {:else if bestRssi > -70}
      <SignalMedium class="text-muted-foreground h-4 w-4 shrink-0" />
    {:else if bestRssi > -85}
      <SignalLow class="text-muted-foreground h-4 w-4 shrink-0" />
    {:else}
      <Signal class="text-muted-foreground h-4 w-4 shrink-0" />
    {/if}

    <div class="min-w-0 flex-1">
      {#if ssid}
        <span class="block truncate text-sm font-medium">{ssid}</span>
      {:else if netgroup}
        <span class="block truncate text-sm font-medium">{netgroup.networks[0].bssid}</span>
        <span class="text-muted-foreground text-xs">(Hidden)</span>
      {/if}
      <div class="text-muted-foreground flex items-center gap-2 text-xs">
        {#if isConnected}
          <span class="text-green-600 dark:text-green-400">Connected</span>
        {:else if !isPresent}
          <span>Not in range</span>
        {:else if isSaved}
          <span>Saved</span>
        {/if}
        {#if isPresent}
          <span>{bestRssi} dBm</span>
        {/if}
        {#if netgroup && netgroup.security !== WifiAuthMode.Open}
          <span class="flex items-center gap-0.5">
            <KeyRound class="h-3 w-3" />
          </span>
        {/if}
      </div>
    </div>
  </div>

  <div class="flex shrink-0 items-center gap-1">
    <!-- Primary action: Connect or Disconnect -->
    {#if isConnected}
      <Button variant="ghost" size="icon" onclick={wifiDisconnect} title="Disconnect">
        <WifiOff class="h-4 w-4" />
      </Button>
    {:else if isSaved}
      <Button variant="ghost" size="icon" onclick={wifiConnect} title="Connect">
        <ArrowRight class="h-4 w-4 text-green-500" />
      </Button>
    {:else if netgroup && netgroup.security === WifiAuthMode.Open}
      <Button variant="ghost" size="icon" onclick={() => wifiAuthenticate(null)} title="Connect">
        <ArrowRight class="h-4 w-4 text-green-500" />
      </Button>
    {:else if netgroup}
      <Dialog bind:open={() => connectDialogOpen, handleConnectDialogOpenChange}>
        <DialogTrigger class={buttonVariants({ variant: 'ghost', size: 'icon' })} title="Connect">
          <Link class="h-4 w-4 text-green-500" />
        </DialogTrigger>
        <DialogContent class="sm:max-w-[425px]">
          <DialogHeader>
            <DialogTitle>Connect to {ssid || 'network'}</DialogTitle>
            <DialogDescription>Enter the WiFi password</DialogDescription>
          </DialogHeader>
          <div class="flex flex-row items-center gap-4 py-4">
            <Label for="wifi-password" class="text-right">Password</Label>
            <Input
              id="wifi-password"
              type="password"
              class="col-span-3"
              bind:value={pendingPassword}
            />
          </div>
          <DialogFooter>
            <Button
              type="button"
              onclick={() => wifiAuthenticate(pendingPassword)}
              disabled={!pendingPassword || pendingPassword.length > 63}
            >
              Connect
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    {/if}

    <!-- Edit password (saved + present + secured networks only) -->
    {#if isSaved && netgroup && netgroup.security !== WifiAuthMode.Open}
      <Dialog bind:open={() => editPasswordDialogOpen, handleEditPasswordDialogOpenChange}>
        <DialogTrigger
          class={buttonVariants({ variant: 'ghost', size: 'icon' })}
          title="Edit password"
        >
          <KeyRound class="h-4 w-4" />
        </DialogTrigger>
        <DialogContent class="sm:max-w-[425px]">
          <DialogHeader>
            <DialogTitle>Edit password for {ssid}</DialogTitle>
            <DialogDescription>Enter the new WiFi password</DialogDescription>
          </DialogHeader>
          <div class="flex flex-row items-center gap-4 py-4">
            <Label for="edit-password" class="text-right">Password</Label>
            <Input
              id="edit-password"
              type="password"
              class="col-span-3"
              bind:value={editPassword}
            />
          </div>
          <DialogFooter>
            <Button
              type="button"
              onclick={() => wifiEditPassword(editPassword)}
              disabled={!editPassword || editPassword.length > 63}
            >
              Save
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    {/if}

    <!-- Forget (saved networks only) -->
    {#if isSaved}
      <Button variant="ghost" size="icon" onclick={wifiForget} title="Forget network">
        <Trash2 class="text-destructive h-4 w-4" />
      </Button>
    {/if}

    <!-- Details (present networks only) -->
    {#if netgroup}
      <WiFiDetailsDialog group={netgroup} />
    {/if}
  </div>
</div>
