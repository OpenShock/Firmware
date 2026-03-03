<script lang="ts">
  import WiFiManager from '$lib/components/WiFiManager.svelte';
  import GpioPinSelector from '$lib/components/GpioPinSelector.svelte';
  import { SerializeSetRfTxPinCommand } from '$lib/Serializers/SetRfTxPinCommand';
  import { SerializeSetEstopPinCommand } from '$lib/Serializers/SetEstopPinCommand';
  import { SerializeSetEstopEnabledCommand } from '$lib/Serializers/SetEstopEnabledCommand';
  import { SerializeAccountLinkCommand } from '$lib/Serializers/AccountLinkCommand';
  import { SerializeAccountUnlinkCommand } from '$lib/Serializers/AccountUnlinkCommand';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { HubStateStore } from '$lib/stores';
  import { Button } from '$lib/components/ui/button';
  import { Input } from '$lib/components/ui/input';
  import { Label } from '$lib/components/ui/label';
  import BackendSection from '$lib/components/sections/BackendSection.svelte';
  import OtaSection from '$lib/components/sections/OtaSection.svelte';
  import CaptivePortalSection from '$lib/components/sections/CaptivePortalSection.svelte';
  import { CircleCheck } from '@lucide/svelte';

  function isValidLinkCode(str: string) {
    if (typeof str != 'string') return false;
    for (let i = 0; i < str.length; i++) {
      if (str[i] < '0' || str[i] > '9') return false;
    }
    return true;
  }

  let linkCode: string = $state('');
  let linkCodeValid = $derived(isValidLinkCode(linkCode));
  let accountLinked = $derived($HubStateStore.accountLinked);

  function linkAccount() {
    if (!linkCodeValid) return;
    const data = SerializeAccountLinkCommand(linkCode!);
    WebSocketClient.Instance.Send(data);
  }

  function unlinkAccount() {
    const data = SerializeAccountUnlinkCommand();
    WebSocketClient.Instance.Send(data);
  }

  function toggleEstop() {
    const data = SerializeSetEstopEnabledCommand(!$HubStateStore.config?.estop?.enabled);
    WebSocketClient.Instance.Send(data);
  }
</script>

<div class="flex flex-1 flex-col items-center px-2 py-4">
  <div class="flex w-full max-w-md flex-col gap-6">
    <!-- WiFi Section -->
    <section class="rounded-lg border p-4">
      <WiFiManager />
    </section>

    <!-- Hardware Section -->
    <section class="rounded-lg border p-4">
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
            currentPin={$HubStateStore.config?.rf?.txPin ?? null}
            serializer={SerializeSetRfTxPinCommand}
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
              checked={$HubStateStore.config?.estop?.enabled ?? false}
              onchange={toggleEstop}
              class="h-4 w-4"
            />
          </label>
          <GpioPinSelector
            name="EStop Pin"
            currentPin={$HubStateStore.config?.estop?.gpioPin ?? null}
            serializer={SerializeSetEstopPinCommand}
          />
        </div>
      </div>
    </section>

    <!-- Account Section -->
    <section class="rounded-lg border p-4">
      <div class="flex flex-col gap-4">
        <div class="flex items-center justify-between">
          <div>
            <h3 class="text-lg font-semibold">Account</h3>
            <p class="text-muted-foreground text-sm">Link this device to your OpenShock account.</p>
          </div>
          {#if accountLinked}
            <CircleCheck class="h-6 w-6 text-green-500" />
          {/if}
        </div>

        {#if accountLinked}
          <div
            class="flex items-center justify-between rounded-lg border border-green-500/30 bg-green-500/10 p-3"
          >
            <p class="text-sm font-medium text-green-700 dark:text-green-300">Account linked</p>
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
    </section>

    <!-- Backend Section -->
    <section class="rounded-lg border p-4">
      <BackendSection />
    </section>

    <!-- OTA Section -->
    <section class="rounded-lg border p-4">
      <OtaSection />
    </section>

    <!-- Captive Portal Section -->
    <section class="rounded-lg border p-4">
      <CaptivePortalSection />
    </section>
  </div>
</div>
