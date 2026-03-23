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
  import AccountStep from '$lib/components/steps/AccountStep.svelte';
  import { hubState } from '$lib/stores';
  import { Button } from '$lib/components/ui/button';

  interface Props {
    onComplete: () => void;
  }

  let { onComplete }: Props = $props();

  let currentStep = $state(1);

  let isDIY = $derived(!hubState.hasPredefinedPins);
  let wifiConnected = $derived(hubState.wifiConnectedBSSID !== null);
  let pinConfigured = $derived(hubState.config?.rf?.txPin != null);
  let totalSteps = $derived(isDIY ? 3 : 2);

  let canAdvance = $derived(
    isDIY
      ? currentStep === 1
        ? pinConfigured
        : currentStep === 2
          ? wifiConnected
          : false
      : currentStep === 1
        ? wifiConnected
        : false
  );

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

        <StepperItem step={isDIY ? 2 : 1}>
          <StepperTrigger>
            <StepperIndicator />
            <div class="hidden sm:block">
              <StepperTitle>WiFi</StepperTitle>
              <StepperDescription>Network setup</StepperDescription>
            </div>
          </StepperTrigger>
          <StepperSeparator />
        </StepperItem>

        <StepperItem step={isDIY ? 3 : 2}>
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
        {#if isDIY}
          {#if currentStep === 1}
            <PinsStep />
          {:else if currentStep === 2}
            <WiFiStep />
          {:else}
            <AccountStep />
          {/if}
        {:else if currentStep === 1}
          <WiFiStep />
        {:else}
          <AccountStep />
        {/if}
      </div>

      <div class="flex justify-between">
        <StepperPrevious />

        {#if isLastStep}
          <Button onclick={onComplete} disabled={!canFinish}>
            {canFinish ? 'Done' : 'Link account first'}
          </Button>
        {:else}
          <StepperNext disabled={!canAdvance}>
            {#if !canAdvance}
              {isDIY && currentStep === 1 ? 'Configure pins first' : 'Connect WiFi first'}
            {:else}
              Next
            {/if}
          </StepperNext>
        {/if}
      </div>
    </Stepper>
  </div>
</div>
