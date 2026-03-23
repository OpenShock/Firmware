<script lang="ts" module>
  import { Wifi, Zap, Cpu, User, Download, type Icon } from '@lucide/svelte';

  export type AdvancedSection = 'menu' | 'wifi' | 'shocker' | 'hardware' | 'account' | 'ota';

  interface AdvancedSectionDef {
    id: AdvancedSection;
    label: string;
    description: string;
    icon: Icon;
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
  import WiFiManager from '$lib/components/WiFiManager.svelte';
  import TestStep from '$lib/components/steps/TestStep.svelte';
  import GpioPinSelector from '$lib/components/GpioPinSelector.svelte';
  import {
    linkAccount as apiLinkAccount,
    unlinkAccount as apiUnlinkAccount,
    setEstopEnabled,
    setRfTxPin,
    setEstopPin,
  } from '$lib/api';
  import { hubState, ViewModeStore } from '$lib/stores';
  import { Button } from '$lib/components/ui/button';
  import { Input } from '$lib/components/ui/input';
  import { Label } from '$lib/components/ui/label';
  import OtaSection from '$lib/components/sections/OtaSection.svelte';
  import { CircleCheck, ChevronRight, ArrowLeft } from '@lucide/svelte';

  interface Props {
    activeSection?: AdvancedSection;
  }

  let { activeSection = $bindable<AdvancedSection>('menu') }: Props = $props();

  function isValidLinkCode(str: string) {
    if (typeof str != 'string') return false;
    for (let i = 0; i < str.length; i++) {
      if (str[i] < '0' || str[i] > '9') return false;
    }
    return true;
  }

  let linkCode: string = $state('');
  let linkCodeValid = $derived(isValidLinkCode(linkCode));
  let accountLinked = $derived(hubState.accountLinked);

  async function linkAccount() {
    if (!linkCodeValid) return;
    await apiLinkAccount(linkCode!);
  }

  async function unlinkAccount() {
    await apiUnlinkAccount();
  }

  async function toggleEstop() {
    await setEstopEnabled(!(hubState.config?.estop?.enabled ?? false));
  }

</script>

<div class="flex flex-1 flex-col items-center px-2 py-4">
  <div class="flex w-full max-w-md flex-1 flex-col">
    {#if activeSection === 'menu'}
      <Button variant="ghost" size="sm" class="mb-2 self-start" onclick={() => ViewModeStore.set('landing')}>
        <ArrowLeft class="mr-1.5 h-4 w-4" />
        Back
      </Button>
      <div class="flex flex-col gap-1">
        {#each advancedSections as section}
          <button
            class="hover:bg-muted/50 flex items-center gap-3 rounded-lg p-3 text-left transition-colors"
            onclick={() => (activeSection = section.id)}
          >
            <section.icon class="text-muted-foreground h-5 w-5 shrink-0" />
            <div class="min-w-0 flex-1">
              <p class="text-sm font-medium">{section.label}</p>
              <p class="text-muted-foreground text-xs">{section.description}</p>
            </div>
            <ChevronRight class="text-muted-foreground h-4 w-4 shrink-0" />
          </button>
        {/each}
      </div>
    {:else}
      <Button variant="ghost" size="sm" class="mb-2 self-start" onclick={() => (activeSection = 'menu')}>
        <ArrowLeft class="mr-1.5 h-4 w-4" />
        Back
      </Button>

      <div class="flex-1 rounded-lg border p-4">
        {#if activeSection === 'wifi'}
          <div class="flex flex-col gap-4">
            <div>
              <h3 class="text-lg font-semibold">WiFi</h3>
              <p class="text-muted-foreground text-sm">Manage wireless networks.</p>
            </div>
            <WiFiManager />
          </div>
        {:else if activeSection === 'shocker'}
          <TestStep />
        {:else if activeSection === 'hardware'}
          <div class="flex flex-col gap-4">
            <div>
              <h3 class="text-lg font-semibold">Hardware</h3>
              <p class="text-muted-foreground text-sm">GPIO pin configuration.</p>
            </div>

            <div class="rounded-lg border p-3">
              <p class="text-muted-foreground mb-3 text-xs">
                The RF transmitter pin controls the 433 MHz radio used to communicate with shockers.
              </p>
              <GpioPinSelector
                name="RF TX Pin"
                currentPin={hubState.config?.rf?.txPin ?? null}
                setter={setRfTxPin}
              />
            </div>

            <div class="rounded-lg border p-3">
              <p class="text-muted-foreground mb-3 text-xs">
                The emergency stop pin provides a hardware kill switch for all shocker output.
              </p>
              <label class="mb-3 flex cursor-pointer items-center justify-between">
                <p class="text-sm font-medium">EStop Enabled</p>
                <input
                  type="checkbox"
                  checked={hubState.config?.estop?.enabled ?? false}
                  onchange={toggleEstop}
                  class="h-4 w-4"
                />
              </label>
              <GpioPinSelector
                name="EStop Pin"
                currentPin={hubState.config?.estop?.gpioPin ?? null}
                setter={setEstopPin}
              />
            </div>
          </div>
        {:else if activeSection === 'account'}
          <div class="flex flex-col gap-4">
            <div>
              <h3 class="text-lg font-semibold">Account</h3>
              <p class="text-muted-foreground text-sm">Link this device to your OpenShock account.</p>
            </div>

            {#if accountLinked}
              <div class="flex items-center justify-between rounded-lg border border-green-500/30 bg-green-500/10 p-3">
                <div class="flex items-center gap-2">
                  <CircleCheck class="h-5 w-5 text-green-500" />
                  <p class="text-sm font-medium text-green-700 dark:text-green-300">Account linked</p>
                </div>
                <Button variant="outline" size="sm" onclick={unlinkAccount}>Unlink</Button>
              </div>
            {:else}
              <div class="flex flex-col gap-2">
                <Label for="adv-link-code">Link Code</Label>
                <p class="text-muted-foreground text-xs">
                  Find your link code on the OpenShock website under device settings.
                </p>
                <div class="flex gap-2">
                  <Input
                    class={linkCodeValid ? '' : 'input-error'}
                    type="text"
                    id="adv-link-code"
                    inputmode="numeric"
                    pattern="[0-9]*"
                    placeholder="Enter link code"
                    bind:value={linkCode}
                  />
                  <Button onclick={linkAccount} disabled={!linkCodeValid || linkCode.length < 6}>
                    Link
                  </Button>
                </div>
              </div>
            {/if}
          </div>
        {:else if activeSection === 'ota'}
          <OtaSection />
        {/if}
      </div>
    {/if}
  </div>
</div>
