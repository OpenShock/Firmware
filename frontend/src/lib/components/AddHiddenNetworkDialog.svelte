<script lang="ts">
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { SerializeWifiNetworkSaveCommand } from '$lib/Serializers/WifiNetworkSaveCommand';
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

  let dialogOpen = $state(false);
  let ssid = $state('');
  let password = $state('');

  let canSave = $derived(ssid.length > 0 && ssid.length <= 31);

  function handleSave() {
    if (!canSave) return;
    const data = SerializeWifiNetworkSaveCommand(ssid, password || null, true);
    WebSocketClient.Instance.Send(data);
    dialogOpen = false;
    ssid = '';
    password = '';
  }

  function handleOpenChange(open: boolean) {
    dialogOpen = open;
    if (!open) {
      ssid = '';
      password = '';
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
      <DialogDescription
        >Enter the network name and password for a hidden WiFi network</DialogDescription
      >
    </DialogHeader>
    <div class="flex flex-col gap-4 py-4">
      <div class="flex flex-row items-center gap-4">
        <Label for="hidden-ssid" class="w-20 text-right">SSID</Label>
        <Input id="hidden-ssid" class="flex-1" placeholder="Network name" bind:value={ssid} />
      </div>
      <div class="flex flex-row items-center gap-4">
        <Label for="hidden-password" class="w-20 text-right">Password</Label>
        <Input
          id="hidden-password"
          class="flex-1"
          type="password"
          placeholder="Leave empty for open networks"
          bind:value={password}
        />
      </div>
    </div>
    <DialogFooter>
      <Button type="button" onclick={handleSave} disabled={!canSave}>Save & Connect</Button>
    </DialogFooter>
  </DialogContent>
</Dialog>
