<script lang="ts">
  import { HubStateStore } from '$lib/stores';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { WifiAuthMode } from '$lib/_fbs/open-shock/serialization/types/wifi-auth-mode';
  import { SerializeWifiNetworkDisconnectCommand } from '$lib/Serializers/WifiNetworkDisconnectCommand';
  import { SerializeWifiNetworkConnectCommand } from '$lib/Serializers/WifiNetworkConnectCommand';
  import { SerializeWifiNetworkSaveCommand } from '$lib/Serializers/WifiNetworkSaveCommand';
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
  import { ArrowRight, Link, Wifi } from '@lucide/svelte';

  interface Props {
    netgroup: WiFiNetworkGroup;
  }

  let { netgroup }: Props = $props();

  let connectedBSSID = $derived($HubStateStore.wifiConnectedBSSID);

  let dialogOpen = $state(false);
  let pendingPassword = $state<string | null>(null);

  function wifiAuthenticate(ssid: string, password: string | null) {
    dialogOpen = false;
    const data = SerializeWifiNetworkSaveCommand(ssid, password, true);
    WebSocketClient.Instance.Send(data);
  }
  function wifiConnect(item: WiFiNetworkGroup) {
    const data = SerializeWifiNetworkConnectCommand(item.ssid);
    WebSocketClient.Instance.Send(data);
  }
  function wifiDisconnect(item: WiFiNetworkGroup) {
    const data = SerializeWifiNetworkDisconnectCommand();
    WebSocketClient.Instance.Send(data);
  }

  function handleWifiAuthDialogOpenChange(open: boolean) {
    dialogOpen = open;
    if (!open) {
      pendingPassword = null;
    }
  }
</script>

<div class="mb-2 flex items-center justify-between p-2">
  <span class="flex flex-row">
    {#if netgroup.networks.some((n) => n.bssid === connectedBSSID)}
      <Wifi color="#22c55e" />
    {:else}
      <Wifi />
    {/if}
    {#if netgroup.ssid}
      <span class="ml-2">{netgroup.ssid}</span>
    {:else}
      <span class="ml-2">{netgroup.networks[0].bssid}</span><span class="ml-1 text-gray-500"
        >(Hidden)</span
      >
    {/if}
  </span>
  <div>
    {#if netgroup.saved}
      <Button onclick={() => wifiConnect(netgroup)}>
        <ArrowRight color="#22c55e" />
      </Button>
    {:else if netgroup.security === WifiAuthMode.Open}
      <Button onclick={() => wifiAuthenticate(netgroup.ssid, null)}>
        <ArrowRight color="#22c55e" />
      </Button>
    {:else}
      <Dialog bind:open={() => dialogOpen, handleWifiAuthDialogOpenChange}>
        <DialogTrigger class={buttonVariants({ variant: 'outline' })}>
          <Link color="#22c55e" />
        </DialogTrigger>
        <DialogContent class="sm:max-w-[425px]">
          <DialogHeader>
            <DialogTitle>Enter password</DialogTitle>
            <DialogDescription>Enter the password for the network</DialogDescription>
          </DialogHeader>
          <div class="flex flex-row items-center gap-4 py-4">
            <Label for="name" class="text-right">Password</Label>
            <Input id="name" class="col-span-3" bind:value={pendingPassword} />
          </div>
          <DialogFooter>
            <Button
              type="button"
              onclick={() => wifiAuthenticate(netgroup.ssid, pendingPassword)}
              disabled={!pendingPassword || pendingPassword.length > 63}
            >
              Save changes
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    {/if}
    <WiFiDetailsDialog group={netgroup} />
  </div>
</div>
