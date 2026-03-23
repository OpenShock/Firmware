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
  import PinsStep from '$lib/components/steps/HardwareStep.svelte';
  import WiFiStep from '$lib/components/steps/WiFiStep.svelte';
  import TestStep from '$lib/components/steps/TestStep.svelte';
  import AccountStep from '$lib/components/steps/AccountStep.svelte';
  import { hubState, ViewModeStore } from '$lib/stores';
  import { Button } from '$lib/components/ui/button';

  interface Props {
    onComplete: () => void;
  }

  let { onComplete }: Props = $props();

  let currentStep = $state(1);

  let isDIY = $derived(!hubState.hasPredefinedPins);
  let wifiConnected = $derived(hubState.wifiConnectedBSSID !== null);
  let pinConfigured = $derived(hubState.config?.rf?.txPin != null);

  // Steps: DIY = Pins, Test, WiFi, Account (4)  |  Prebuilt = Test, WiFi, Account (3)
  let totalSteps = $derived(isDIY ? 4 : 3);

  // Step number mapping
  let testStep = $derived(isDIY ? 2 : 1);
  let wifiStep = $derived(isDIY ? 3 : 2);
  let accountStep = $derived(isDIY ? 4 : 3);

  let canAdvance = $derived.by(() => {
    if (isDIY && currentStep === 1) return pinConfigured;
    // Test step: always advanceable (testing is optional)
    if (currentStep === testStep) return true;
    if (currentStep === wifiStep) return wifiConnected;
    return false;
  });

  let isLastStep = $derived(currentStep === totalSteps);
  let canFinish = $derived(wifiConnected && hubState.accountLinked);
</script>

<div class="flex flex-1 flex-col items-center px-2 py-4">
  <div class="flex w-full max-w-md flex-1 flex-col">
    <Stepper bind:value={currentStep} linear class="flex-1">
      <StepperNav>
        {#if isDIY}
          <StepperItem step={1}>
            <StepperTrigger>
              <StepperIndicator />
              <div class="hidden sm:block">
                <StepperTitle>Pins</StepperTitle>
                <StepperDescription>Hardware setup</StepperDescription>
              </div>
            </StepperTrigger>
            <StepperSeparator />
          </StepperItem>
        {/if}

        <StepperItem step={testStep}>
          <StepperTrigger>
            <StepperIndicator />
            <div class="hidden sm:block">
              <StepperTitle>Test</StepperTitle>
              <StepperDescription>Verify shocker</StepperDescription>
            </div>
          </StepperTrigger>
          <StepperSeparator />
        </StepperItem>

        <StepperItem step={wifiStep}>
          <StepperTrigger>
            <StepperIndicator />
            <div class="hidden sm:block">
              <StepperTitle>WiFi</StepperTitle>
              <StepperDescription>Network setup</StepperDescription>
            </div>
          </StepperTrigger>
          <StepperSeparator />
        </StepperItem>

        <StepperItem step={accountStep}>
          <StepperTrigger>
            <StepperIndicator />
            <div class="hidden sm:block">
              <StepperTitle>Account</StepperTitle>
              <StepperDescription>Link device</StepperDescription>
            </div>
          </StepperTrigger>
        </StepperItem>
      </StepperNav>

      <div class="flex-1 rounded-lg border p-4">
        {#if isDIY && currentStep === 1}
          <PinsStep />
        {:else if currentStep === testStep}
          <TestStep />
        {:else if currentStep === wifiStep}
          <WiFiStep />
        {:else}
          <AccountStep />
        {/if}
      </div>

      <div class="flex justify-between">
        {#if currentStep === 1}
          <Button variant="outline" onclick={() => ViewModeStore.set('landing')}>Back</Button>
        {:else}
          <StepperPrevious />
        {/if}

        {#if isLastStep}
          <Button onclick={onComplete} disabled={!canFinish}>
            {canFinish ? 'Done' : 'Link account first'}
          </Button>
        {:else}
          <StepperNext disabled={!canAdvance}>
            {#if !canAdvance}
              {isDIY && currentStep === 1 ? 'Configure pins first' : currentStep === wifiStep ? 'Connect WiFi first' : ''}
            {:else}
              Next
            {/if}
          </StepperNext>
        {/if}
      </div>
    </Stepper>
  </div>
</div>
