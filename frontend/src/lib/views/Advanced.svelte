<script lang="ts" module>
  import { Wifi, Zap, Cpu, User, Download } from '@lucide/svelte';
  import type { Component } from 'svelte';

  export type AdvancedSection = 'menu' | 'wifi' | 'shocker' | 'hardware' | 'account' | 'ota';

  interface AdvancedSectionDef {
    id: AdvancedSection;
    label: string;
    description: string;
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    icon: Component<any>;
  }

  const advancedSections: AdvancedSectionDef[] = [
    { id: 'wifi', label: 'WiFi', description: 'Network configuration', icon: Wifi },
    { id: 'shocker', label: 'Shocker', description: 'Test your shockers', icon: Zap },
    { id: 'hardware', label: 'Hardware', description: 'GPIO pin configuration', icon: Cpu },
    { id: 'account', label: 'Account', description: 'Link to OpenShock', icon: User },
    { id: 'ota', label: 'Updates', description: 'OTA update settings', icon: Download },
  ];
</script>

<script lang="ts">
  import AccountStep from '#lib/components/steps/AccountStep.svelte';
  import HardwareStep from '#lib/components/steps/HardwareStep.svelte';
  import TestStep from '#lib/components/steps/TestStep.svelte';
  import WiFiStep from '#lib/components/steps/WiFiStep.svelte';
  import OtaSection from '#lib/components/sections/OtaSection.svelte';
  import SectionHeader from '#lib/components/SectionHeader.svelte';
  import { ViewModeStore } from '#lib/stores/index.js';
  import { Button } from '@openshock/svelte-core/components/ui/button';
  import { ChevronRight, ArrowLeft } from '@lucide/svelte';

  interface Props {
    activeSection?: AdvancedSection;
  }

  let { activeSection = $bindable<AdvancedSection>('menu') }: Props = $props();
</script>

<div class="flex flex-1 flex-col items-center px-2 py-4">
  <div class="flex w-full max-w-md flex-1 flex-col">
    {#if activeSection === 'menu'}
      <Button
        variant="ghost"
        size="sm"
        class="mb-2 self-start"
        onclick={() => ViewModeStore.set('landing')}
      >
        <ArrowLeft class="mr-1.5 h-4 w-4" />
        Back
      </Button>
      <SectionHeader
        title="Advanced Setup"
        description="Configure each part of the hub on its own."
      />
      <div class="mt-4 flex flex-col gap-1">
        {#each advancedSections as { id, label, description, icon: Icon } (id)}
          <button
            class="hover:bg-muted/50 flex items-center gap-3 rounded-lg p-3 text-left transition-colors"
            onclick={() => (activeSection = id)}
          >
            <Icon class="text-muted-foreground h-5 w-5 shrink-0" />
            <div class="min-w-0 flex-1">
              <p class="text-sm font-medium">{label}</p>
              <p class="text-muted-foreground text-xs">{description}</p>
            </div>
            <ChevronRight class="text-muted-foreground h-4 w-4 shrink-0" />
          </button>
        {/each}
      </div>
    {:else}
      <Button
        variant="ghost"
        size="sm"
        class="mb-2 self-start"
        onclick={() => (activeSection = 'menu')}
      >
        <ArrowLeft class="mr-1.5 h-4 w-4" />
        Back
      </Button>

      <div class="flex-1 rounded-lg border p-4">
        {#if activeSection === 'wifi'}
          <WiFiStep />
        {:else if activeSection === 'shocker'}
          <TestStep />
        {:else if activeSection === 'hardware'}
          <HardwareStep />
        {:else if activeSection === 'account'}
          <AccountStep />
        {:else if activeSection === 'ota'}
          <OtaSection />
        {/if}
      </div>
    {/if}
  </div>
</div>
