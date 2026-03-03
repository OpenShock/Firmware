<script lang="ts">
  import {
    Stepper,
    StepperNav,
    StepperItem,
    StepperTrigger,
    StepperIndicator,
    StepperSeparator,
    StepperTitle,
    StepperDescription,
    StepperNext,
    StepperPrevious,
  } from '$lib/components/ui/stepper';
  import WiFiStep from '$lib/components/steps/WiFiStep.svelte';
  import HardwareStep from '$lib/components/steps/HardwareStep.svelte';
  import AccountStep from '$lib/components/steps/AccountStep.svelte';
  import AdvancedView from '$lib/components/AdvancedView.svelte';
  import { HubStateStore, ViewModeStore } from '$lib/stores';

  let currentStep = $state(1);
  let wifiConnected = $derived($HubStateStore.wifiConnectedBSSID !== null);
</script>

{#if $ViewModeStore === 'advanced'}
  <AdvancedView />
{:else}
  <div class="flex flex-1 flex-col items-center px-2 py-4">
    <div class="w-full max-w-md">
      <Stepper bind:value={currentStep} linear>
        <!-- Step Navigation Header -->
        <StepperNav>
          <StepperItem step={1}>
            <StepperTrigger>
              <StepperIndicator />
              <div class="hidden sm:block">
                <StepperTitle>WiFi</StepperTitle>
                <StepperDescription>Network setup</StepperDescription>
              </div>
            </StepperTrigger>
            <StepperSeparator />
          </StepperItem>

          <StepperItem step={2}>
            <StepperTrigger>
              <StepperIndicator />
              <div class="hidden sm:block">
                <StepperTitle>Hardware</StepperTitle>
                <StepperDescription>Pin config</StepperDescription>
              </div>
            </StepperTrigger>
            <StepperSeparator />
          </StepperItem>

          <StepperItem step={3}>
            <StepperTrigger>
              <StepperIndicator />
              <div class="hidden sm:block">
                <StepperTitle>Account</StepperTitle>
                <StepperDescription>Link device</StepperDescription>
              </div>
            </StepperTrigger>
          </StepperItem>
        </StepperNav>

        <!-- Step Content -->
        <div class="rounded-lg border p-4">
          {#if currentStep === 1}
            <WiFiStep />
          {:else if currentStep === 2}
            <HardwareStep />
          {:else if currentStep === 3}
            <AccountStep />
          {/if}
        </div>

        <!-- Navigation Buttons -->
        <div class="flex justify-between">
          <StepperPrevious />
          {#if currentStep < 3}
            <StepperNext disabled={currentStep === 1 && !wifiConnected}>
              {#if currentStep === 1 && !wifiConnected}
                Connect WiFi first
              {:else}
                Next
              {/if}
            </StepperNext>
          {/if}
        </div>
      </Stepper>
    </div>
  </div>
{/if}
