<script lang="ts">
  import { hubState } from '$lib/stores';
  import { OtaUpdateChannel } from '$lib/_fbs/open-shock/serialization/configuration/ota-update-channel';
  import {
    setOtaEnabled,
    setOtaDomain,
    setOtaChannel,
    setOtaCheckInterval,
    setOtaAllowBackendManagement,
    setOtaRequireManualApproval,
    checkOtaUpdates,
  } from '$lib/api';
  import { Button } from '$lib/components/ui/button';
  import { Input } from '$lib/components/ui/input';
  import { Label } from '$lib/components/ui/label';

  let otaConfig = $derived(hubState.config?.otaUpdate);

  let repoDomain = $state('');
  let checkInterval = $state(0);

  $effect(() => {
    if (otaConfig?.repoDomain) repoDomain = otaConfig.repoDomain;
    if (otaConfig?.checkInterval) checkInterval = otaConfig.checkInterval;
  });

  function toggleEnabled() {
    setOtaEnabled(!otaConfig?.isEnabled);
  }

  function saveDomain() {
    setOtaDomain(repoDomain);
  }

  function setChannel(channel: string) {
    setOtaChannel(channel);
  }

  function saveCheckInterval() {
    setOtaCheckInterval(checkInterval);
  }

  function toggleBackendManagement() {
    setOtaAllowBackendManagement(!otaConfig?.allowBackendManagement);
  }

  function toggleManualApproval() {
    setOtaRequireManualApproval(!otaConfig?.requireManualApproval);
  }

  function checkForUpdates() {
    const channelName = channelToString(otaConfig?.updateChannel ?? OtaUpdateChannel.Stable);
    checkOtaUpdates(channelName);
  }

  function channelToString(ch: OtaUpdateChannel): string {
    switch (ch) {
      case OtaUpdateChannel.Stable:
        return 'stable';
      case OtaUpdateChannel.Beta:
        return 'beta';
      case OtaUpdateChannel.Develop:
        return 'develop';
      default:
        return 'stable';
    }
  }
</script>

<div class="flex flex-col gap-4">
  <div>
    <h3 class="text-lg font-semibold">OTA Updates</h3>
    <p class="text-muted-foreground text-sm">Over-the-air firmware update settings.</p>
  </div>

  <div class="flex flex-col gap-4">
    <!-- Enabled toggle -->
    <label class="flex cursor-pointer items-center justify-between rounded-lg border p-3">
      <div>
        <p class="text-sm font-medium">OTA Updates Enabled</p>
        <p class="text-muted-foreground text-xs">
          Allow the device to check for and install firmware updates.
        </p>
      </div>
      <input
        type="checkbox"
        checked={otaConfig?.isEnabled ?? false}
        onchange={toggleEnabled}
        class="h-4 w-4"
      />
    </label>

    <!-- Repository Domain -->
    <div class="flex flex-col gap-2">
      <Label for="ota-domain">Repository Domain</Label>
      <div class="flex gap-2">
        <Input id="ota-domain" type="text" bind:value={repoDomain} placeholder="repo.openshock.org" />
        <Button size="sm" onclick={saveDomain}>Save</Button>
      </div>
    </div>

    <!-- Update Channel -->
    <div class="flex flex-col gap-2">
      <Label for="ota-channel">Update Channel</Label>
      <div class="flex gap-2">
        <Button
          size="sm"
          variant={otaConfig?.updateChannel === OtaUpdateChannel.Stable ? 'default' : 'outline'}
          onclick={() => setChannel('stable')}
        >
          Stable
        </Button>
        <Button
          size="sm"
          variant={otaConfig?.updateChannel === OtaUpdateChannel.Beta ? 'default' : 'outline'}
          onclick={() => setChannel('beta')}
        >
          Beta
        </Button>
        <Button
          size="sm"
          variant={otaConfig?.updateChannel === OtaUpdateChannel.Develop ? 'default' : 'outline'}
          onclick={() => setChannel('develop')}
        >
          Develop
        </Button>
      </div>
    </div>

    <!-- Check Interval -->
    <div class="flex flex-col gap-2">
      <Label for="ota-interval">Check Interval (minutes)</Label>
      <div class="flex gap-2">
        <Input id="ota-interval" type="number" min={0} max={65535} bind:value={checkInterval} />
        <Button size="sm" onclick={saveCheckInterval}>Save</Button>
      </div>
    </div>

    <!-- Backend Management toggle -->
    <label class="flex cursor-pointer items-center justify-between rounded-lg border p-3">
      <div>
        <p class="text-sm font-medium">Allow Backend Management</p>
        <p class="text-muted-foreground text-xs">Let the gateway server trigger updates.</p>
      </div>
      <input
        type="checkbox"
        checked={otaConfig?.allowBackendManagement ?? false}
        onchange={toggleBackendManagement}
        class="h-4 w-4"
      />
    </label>

    <!-- Manual Approval toggle -->
    <label class="flex cursor-pointer items-center justify-between rounded-lg border p-3">
      <div>
        <p class="text-sm font-medium">Require Manual Approval</p>
        <p class="text-muted-foreground text-xs">Prompt before installing updates.</p>
      </div>
      <input
        type="checkbox"
        checked={otaConfig?.requireManualApproval ?? false}
        onchange={toggleManualApproval}
        class="h-4 w-4"
      />
    </label>

    <!-- Check for Updates button -->
    <Button onclick={checkForUpdates}>Check for Updates</Button>
  </div>
</div>
