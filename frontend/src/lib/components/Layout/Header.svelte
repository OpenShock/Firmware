<script lang="ts">
  import LightSwitch from '$lib/components/LightSwitch.svelte';
  import { Button } from '$lib/components/ui/button';
  import { ViewModeStore } from '$lib/stores';
  import { ArrowLeft } from '@lucide/svelte';
</script>

<header
  class="border-border/40 bg-background/95 supports-backdrop-filter:bg-background/60 sticky top-0 z-50 flex w-full flex-row border-b backdrop-blur-sm"
>
  <div class="flex flex-1 flex-row items-center space-x-2 px-4 py-2">
    <Button variant="ghost" size="icon" onclick={() => ViewModeStore.set('landing')} title="Back">
      <ArrowLeft class="h-4 w-4" />
    </Button>

    <a href="/" class="overflow-hidden select-none" aria-label="OpenShock">
      <img
        class="pointer-events-none inline-block h-6 sm:h-10"
        src="/logo.svg"
        alt="OpenShock Logo"
      />
    </a>

    <div class="flex-1"></div>

    <LightSwitch />
  </div>
</header>
