<script lang="ts">
  import { hubState } from '$lib/stores';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { WifiAuthMode } from '$lib/_fbs/open-shock/serialization/types/wifi-auth-mode';
  import { SerializeWifiNetworkConnectCommand } from '$lib/Serializers/WifiNetworkConnectCommand';
  import { SerializeWifiNetworkSaveCommand } from '$lib/Serializers/WifiNetworkSaveCommand';
  import { forgetWifiNetwork } from '$lib/api';
  import { SerializeWifiNetworkDisconnectCommand } from '$lib/Serializers/WifiNetworkDisconnectCommand';
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
    netgroup: WiFiNetworkGroup;
  }

  let { netgroup }: Props = $props();

  let connectedBSSID = $derived(hubState.wifiConnectedBSSID);
  let isConnected = $derived(netgroup.networks.some((n) => n.bssid === connectedBSSID));
  let bestRssi = $derived(netgroup.networks[0]?.rssi ?? -100);

  let connectDialogOpen = $state(false);
  let editPasswordDialogOpen = $state(false);
  let pendingPassword = $state<string | null>(null);
  let editPassword = $state<string | null>(null);

  function wifiAuthenticate(ssid: string, password: string | null) {
    connectDialogOpen = false;
    const data = SerializeWifiNetworkSaveCommand(ssid, password, true);
    WebSocketClient.Instance.Send(data);
  }

  function wifiConnect(item: WiFiNetworkGroup) {
    const data = SerializeWifiNetworkConnectCommand(item.ssid);
    WebSocketClient.Instance.Send(data);
  }

  function wifiDisconnect() {
    const data = SerializeWifiNetworkDisconnectCommand();
    WebSocketClient.Instance.Send(data);
  }

  function wifiForget(ssid: string) {
    forgetWifiNetwork(ssid);
  }

  function wifiEditPassword(ssid: string, password: string | null) {
    editPasswordDialogOpen = false;
    const data = SerializeWifiNetworkSaveCommand(ssid, password, false);
    WebSocketClient.Instance.Send(data);
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
      {#if netgroup.ssid}
        <span class="block truncate text-sm font-medium">{netgroup.ssid}</span>
      {:else}
        <span class="block truncate text-sm font-medium">{netgroup.networks[0].bssid}</span>
        <span class="text-muted-foreground text-xs">(Hidden)</span>
      {/if}
      <div class="text-muted-foreground flex items-center gap-2 text-xs">
        {#if isConnected}
          <span class="text-green-600 dark:text-green-400">Connected</span>
        {:else if netgroup.saved}
          <span>Saved</span>
        {/if}
        <span>{bestRssi} dBm</span>
        {#if netgroup.security !== WifiAuthMode.Open}
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
    {:else if netgroup.saved}
      <Button variant="ghost" size="icon" onclick={() => wifiConnect(netgroup)} title="Connect">
        <ArrowRight class="h-4 w-4 text-green-500" />
      </Button>
    {:else if netgroup.security === WifiAuthMode.Open}
      <Button
        variant="ghost"
        size="icon"
        onclick={() => wifiAuthenticate(netgroup.ssid, null)}
        title="Connect"
      >
        <ArrowRight class="h-4 w-4 text-green-500" />
      </Button>
    {:else}
      <Dialog bind:open={() => connectDialogOpen, handleConnectDialogOpenChange}>
        <DialogTrigger class={buttonVariants({ variant: 'ghost', size: 'icon' })} title="Connect">
          <Link class="h-4 w-4 text-green-500" />
        </DialogTrigger>
        <DialogContent class="sm:max-w-[425px]">
          <DialogHeader>
            <DialogTitle>Connect to {netgroup.ssid || 'network'}</DialogTitle>
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
              onclick={() => wifiAuthenticate(netgroup.ssid, pendingPassword)}
              disabled={!pendingPassword || pendingPassword.length > 63}
            >
              Connect
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    {/if}

    <!-- Edit password (saved networks only) -->
    {#if netgroup.saved && netgroup.security !== WifiAuthMode.Open}
      <Dialog bind:open={() => editPasswordDialogOpen, handleEditPasswordDialogOpenChange}>
        <DialogTrigger
          class={buttonVariants({ variant: 'ghost', size: 'icon' })}
          title="Edit password"
        >
          <KeyRound class="h-4 w-4" />
        </DialogTrigger>
        <DialogContent class="sm:max-w-[425px]">
          <DialogHeader>
            <DialogTitle>Edit password for {netgroup.ssid}</DialogTitle>
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
              onclick={() => wifiEditPassword(netgroup.ssid, editPassword)}
              disabled={!editPassword || editPassword.length > 63}
            >
              Save
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    {/if}

    <!-- Forget (saved networks only) -->
    {#if netgroup.saved}
      <Button
        variant="ghost"
        size="icon"
        onclick={() => wifiForget(netgroup.ssid)}
        title="Forget network"
      >
        <Trash2 class="text-destructive h-4 w-4" />
      </Button>
    {/if}

    <!-- Details -->
    <WiFiDetailsDialog group={netgroup} />
  </div>
</div>
