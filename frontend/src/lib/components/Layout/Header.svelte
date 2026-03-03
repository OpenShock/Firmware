<script lang="ts">
  import LightSwitch from '$lib/components/LightSwitch.svelte';
  import { Button } from '$lib/components/ui/button';
  import { ViewModeStore } from '$lib/stores';
  import { WandSparkles, Settings } from '@lucide/svelte';
</script>

<header
  class="border-border/40 bg-background/95 supports-backdrop-filter:bg-background/60 sticky top-0 z-50 flex w-full flex-row border-b backdrop-blur-sm"
>
  <div class="flex flex-1 flex-row items-center justify-between space-x-2 px-4 py-2">
    <div class="flex items-center space-x-4">
      <a
        href="/"
        class="overflow-hidden select-none lg:ml-0! lg:w-auto"
        data-sveltekit-preload-data="hover"
        aria-label="OpenShock"
      >
        <img
          class="pointer-events-none inline-block h-6 sm:h-10"
          src="/logo.svg"
          alt="OpenShock Logo"
        />
      </a>
    </div>

    <Button
      variant="ghost"
      size="sm"
      onclick={() => ViewModeStore.toggle()}
      title={$ViewModeStore === 'wizard' ? 'Switch to Advanced' : 'Switch to Setup'}
    >
      {#if $ViewModeStore === 'wizard'}
        <Settings class="mr-1.5 h-4 w-4" />
        Advanced
      {:else}
        <WandSparkles class="mr-1.5 h-4 w-4" />
        Setup
      {/if}
    </Button>

    <div class="flex-1"></div>

    <LightSwitch />
  </div>
</header>
