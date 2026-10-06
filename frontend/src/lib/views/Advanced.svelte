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

  // On a phone 'menu' hides the content panel, so this only shows from md up.
  let shownSection = $derived(activeSection === 'menu' ? advancedSections[0].id : activeSection);
</script>

<!-- Phones drill down from the menu into one section. From md up the menu is a sidebar and a
     section is always shown, defaulting to the first. -->
<div class="flex flex-1 flex-col items-center px-2 py-4 md:px-6 md:py-8">
  <div
    class="flex w-full max-w-md flex-1 flex-col md:max-w-5xl md:flex-row md:items-start md:gap-6"
  >
    <nav
      aria-label="Advanced settings"
      class={[
        'flex-col md:sticky md:top-20 md:flex md:w-64 md:shrink-0',
        activeSection === 'menu' ? 'flex' : 'hidden',
      ]}
    >
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
            class="hover:bg-muted/50 focus-visible:ring-ring/50 md:aria-[current=page]:bg-muted flex cursor-pointer items-center gap-3 rounded-lg p-3 text-left transition-colors outline-none focus-visible:ring-3"
            aria-current={id === shownSection ? 'page' : undefined}
            onclick={() => (activeSection = id)}
          >
            <Icon class="text-muted-foreground h-5 w-5 shrink-0" />
            <div class="min-w-0 flex-1">
              <p class="text-sm font-medium">{label}</p>
              <p class="text-muted-foreground text-xs">{description}</p>
            </div>
            <ChevronRight class="text-muted-foreground h-4 w-4 shrink-0 md:hidden" />
          </button>
        {/each}
      </div>
    </nav>

    <div class={['min-w-0 flex-1 flex-col', activeSection === 'menu' ? 'hidden md:flex' : 'flex']}>
      <Button
        variant="ghost"
        size="sm"
        class="mb-2 self-start md:hidden"
        onclick={() => (activeSection = 'menu')}
      >
        <ArrowLeft class="mr-1.5 h-4 w-4" />
        Back
      </Button>

      <div class="flex-1 rounded-lg border p-4 md:flex-none md:p-6">
        {#if shownSection === 'wifi'}
          <WiFiStep />
        {:else if shownSection === 'shocker'}
          <TestStep />
        {:else if shownSection === 'hardware'}
          <HardwareStep />
        {:else if shownSection === 'account'}
          <AccountStep />
        {:else if shownSection === 'ota'}
          <OtaSection />
        {/if}
      </div>
    </div>
  </div>
</div>
