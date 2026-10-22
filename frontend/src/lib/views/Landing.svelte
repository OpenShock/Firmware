<script lang="ts">
  import { ViewModeStore } from '#lib/stores/index.js';
  import { WandSparkles, Settings } from '@lucide/svelte';

  const choices = [
    {
      mode: 'wizard',
      label: 'Guided Setup',
      description: 'Step by step: pins, a test shock, WiFi and your account.',
      icon: WandSparkles,
      primary: true,
    },
    {
      mode: 'advanced',
      label: 'Advanced Setup',
      description: 'Jump straight to any setting, including OTA updates.',
      icon: Settings,
      primary: false,
    },
  ] as const;
</script>

<div class="flex flex-1 flex-col items-center justify-center gap-8 p-8">
  <img class="pointer-events-none h-12 select-none md:h-16" src="/logo.svg" alt="OpenShock Logo" />
  <div class="flex flex-col items-center gap-2 text-center">
    <h1 class="text-2xl font-bold md:text-3xl">Welcome to OpenShock</h1>
    <p class="text-muted-foreground max-w-sm text-sm">Choose how you'd like to set up your hub.</p>
  </div>
  <div class="grid w-full max-w-xs gap-3 sm:max-w-2xl sm:grid-cols-2 sm:gap-4">
    {#each choices as { mode, label, description, icon: Icon, primary } (mode)}
      <button
        class={[
          'focus-visible:ring-ring/50 flex cursor-pointer items-center gap-3 rounded-lg border p-4 text-left transition-colors outline-none focus-visible:ring-3 sm:flex-col sm:items-start sm:p-6',
          primary
            ? 'bg-primary text-primary-foreground hover:bg-primary/90 border-transparent'
            : 'hover:bg-muted/50',
        ]}
        onclick={() => ViewModeStore.set(mode)}
      >
        <Icon class="h-5 w-5 shrink-0 sm:h-6 sm:w-6" />
        <span class="flex flex-col gap-1">
          <span class="font-medium">{label}</span>
          <span
            class={[
              'hidden text-sm sm:block',
              primary ? 'text-primary-foreground/80' : 'text-muted-foreground',
            ]}
          >
            {description}
          </span>
        </span>
      </button>
    {/each}
  </div>
</div>
