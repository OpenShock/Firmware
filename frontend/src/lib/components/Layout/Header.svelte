<script lang="ts">
  import { LightSwitch } from '@openshock/svelte-core/components';
  import { ViewModeStore } from '#lib/stores/index.js';
</script>

<header
  class="border-border/40 bg-background/95 supports-backdrop-filter:bg-background/60 sticky top-0 z-50 w-full border-b px-4 backdrop-blur-sm md:px-6"
>
  <div class="mx-auto flex max-w-5xl flex-row items-center gap-2 py-2">
    <button
      onclick={() => ViewModeStore.set('landing')}
      class="cursor-pointer overflow-hidden select-none"
      aria-label="OpenShock"
    >
      <img
        class="pointer-events-none inline-block h-6 md:h-7"
        src="/logo.svg"
        alt="OpenShock Logo"
      />
    </button>

    <div class="flex-1"></div>

    <LightSwitch />
  </div>
</header>
