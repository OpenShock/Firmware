<script lang="ts">
  import SectionHeader from '#lib/components/SectionHeader.svelte';
  import { hubState } from '#lib/stores/index.js';
  import { TriangleAlert } from '@lucide/svelte';

  let alwaysEnabled = $derived(hubState.config?.captivePortal?.alwaysEnabled ?? false);
</script>

<div class="flex flex-col gap-4">
  <SectionHeader title="Captive Portal" description="Web configuration portal settings." />

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
    <div class="border-warning/30 bg-warning/10 flex items-center gap-2 rounded-lg border p-3">
      <TriangleAlert class="text-warning h-5 w-5 shrink-0" />
      <p class="text-xs">
        The captive portal will close automatically when the hub connects to the gateway. Enable
        "Always Enabled" via serial commands to keep it running.
      </p>
    </div>
  {/if}
</div>
