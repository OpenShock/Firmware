<script lang="ts">
  import { HubStateStore } from '$lib/stores';
  import { TriangleAlert } from '@lucide/svelte';

  let alwaysEnabled = $derived($HubStateStore.config?.captivePortal?.alwaysEnabled ?? false);
</script>

<div class="flex flex-col gap-4">
  <div>
    <h3 class="text-lg font-semibold">Captive Portal</h3>
    <p class="text-muted-foreground text-sm">Web configuration portal settings.</p>
  </div>

  <label class="flex cursor-pointer items-center justify-between rounded-lg border p-3">
    <div>
      <p class="text-sm font-medium">Always Enabled</p>
      <p class="text-muted-foreground text-xs">
        Keep the portal running even after connecting to the gateway.
      </p>
    </div>
    <input type="checkbox" checked={alwaysEnabled} disabled class="h-4 w-4" />
  </label>

  {#if !alwaysEnabled}
    <div
      class="flex items-center gap-2 rounded-lg border border-yellow-500/30 bg-yellow-500/10 p-3"
    >
      <TriangleAlert class="h-5 w-5 shrink-0 text-yellow-500" />
      <p class="text-xs text-yellow-700 dark:text-yellow-300">
        The captive portal will close automatically when the device connects to the gateway. Enable
        "Always Enabled" via serial commands to keep it running.
      </p>
    </div>
  {/if}
</div>
