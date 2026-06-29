<script lang="ts">
  import { Button } from '$lib/components/ui/button';
  import { ViewModeStore } from '$lib/stores';
  import { WandSparkles, Settings } from '@lucide/svelte';
</script>

<div class="flex flex-1 flex-col items-center justify-center gap-8 p-8">
  <img class="pointer-events-none h-12 select-none" src="/logo.svg" alt="OpenShock Logo" />
  <h1 class="text-2xl font-bold">Welcome to OpenShock</h1>
  <p class="text-muted-foreground max-w-sm text-center text-sm">
    Choose how you'd like to set up your device.
  </p>
  <div class="flex w-full max-w-xs flex-col gap-3">
    <Button size="lg" onclick={() => ViewModeStore.set('wizard')}>
      <WandSparkles class="mr-2 h-5 w-5" />
      Guided Setup
    </Button>
    <Button size="lg" variant="outline" onclick={() => ViewModeStore.set('advanced')}>
      <Settings class="mr-2 h-5 w-5" />
      Advanced Setup
    </Button>
  </div>
</div>
