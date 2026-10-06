<script lang="ts">
  import { WifiAuthMode } from '#lib/_fbs/open-shock/serialization/types/wifi-auth-mode.js';
  import { buttonVariants } from '@openshock/svelte-core/components/ui/button';
  import type { WiFiNetworkGroup } from '#lib/types/index.js';
  import {
    Dialog,
    DialogContent,
    DialogHeader,
    DialogTitle,
    DialogTrigger,
  } from '@openshock/svelte-core/components/ui/dialog';

  import { Settings } from '@lucide/svelte';

  type Props = {
    group: WiFiNetworkGroup;
  };

  let { group }: Props = $props();

  function GetWifiAuthModeString(type: WifiAuthMode) {
    switch (type) {
      case WifiAuthMode.Open:
        return 'Open';
      case WifiAuthMode.WEP:
        return 'WEP';
      case WifiAuthMode.WPA_PSK:
        return 'WPA PSK';
      case WifiAuthMode.WPA2_PSK:
        return 'WPA2 PSK';
      case WifiAuthMode.WPA_WPA2_PSK:
        return 'WPA/WPA2 PSK';
      case WifiAuthMode.WPA2_ENTERPRISE:
        return 'WPA2 Enterprise';
      case WifiAuthMode.WPA3_PSK:
        return 'WPA3 PSK';
      case WifiAuthMode.WPA2_WPA3_PSK:
        return 'WPA2/WPA3 PSK';
      case WifiAuthMode.WAPI_PSK:
        return 'WAPI PSK';
      case WifiAuthMode.UNKNOWN:
      default:
        return 'Unknown';
    }
  }

  let rows = $derived([
    { key: 'SSID', value: group.ssid },
    { key: 'Security', value: GetWifiAuthModeString(group.security) },
    { key: 'Saved', value: group.saved.toString() },
  ]);
</script>

<Dialog>
  <DialogTrigger class={buttonVariants({ variant: 'outline' })}>
    <Settings />
  </DialogTrigger>
  <DialogContent class="sm:max-w-[425px]">
    <DialogHeader>
      <DialogTitle>Network info</DialogTitle>
    </DialogHeader>
    <div>
      {#each rows as row (row.key)}
        <span class="flex justify-between"
          ><span class="font-bold">{row.key}:</span><span class="text-muted-foreground"
            >{row.value}</span
          ></span
        >
      {/each}
    </div>
    <!-- Per-AP info -->
    <div>
      <h3 class="text-sm font-semibold">Access Points</h3>
      <!-- Scrollable list of APs -->
      <div class="flex max-h-64 flex-col space-y-2 overflow-y-auto p-2">
        {#each group.networks as network (network.bssid)}
          <div class="flex items-center justify-between rounded-md border p-2">
            <span class="font-bold">{network.bssid}</span>
            <span class="text-muted-foreground">{network.rssi} dBm</span>
            <span class="text-muted-foreground">Channel {network.channel}</span>
          </div>
        {/each}
      </div>
    </div>
  </DialogContent>
</Dialog>
