<script lang="ts">
  import SectionHeader from '#lib/components/SectionHeader.svelte';
  import SettingSwitch from '#lib/components/SettingSwitch.svelte';
  import { hubState } from '#lib/stores/index.js';
  import { OtaUpdateChannel } from '#lib/_fbs/open-shock/serialization/configuration/ota-update-channel.js';
  import {
    setOtaEnabled,
    setOtaChannel,
    setOtaCheckInterval,
    setOtaAllowBackendManagement,
    setOtaRequireManualApproval,
    checkOtaUpdates,
  } from '#lib/api.js';
  import { Button } from '@openshock/svelte-core/components/ui/button';
  import { Input } from '@openshock/svelte-core/components/ui/input';
  import { Label } from '@openshock/svelte-core/components/ui/label';

  const channels = [
    { value: OtaUpdateChannel.Stable, label: 'Stable' },
    { value: OtaUpdateChannel.Beta, label: 'Beta' },
    { value: OtaUpdateChannel.Develop, label: 'Develop' },
  ];

  let otaConfig = $derived(hubState.config?.otaUpdate);

  // Unsaved edits; undefined shows the hub's stored value. A cleared number input binds null.
  let checkIntervalEdit = $state<number | null | undefined>();

  let checkInterval = $derived(
    checkIntervalEdit !== undefined ? checkIntervalEdit : (otaConfig?.checkInterval ?? 0)
  );

  let intervalValid = $derived(
    checkInterval !== null &&
      Number.isInteger(checkInterval) &&
      checkInterval >= 0 &&
      checkInterval <= 65535
  );
  let canSaveInterval = $derived(
    checkIntervalEdit !== undefined && intervalValid && checkInterval !== otaConfig?.checkInterval
  );

  let saving = $state(false);

  async function saveCheckInterval(e: SubmitEvent) {
    e.preventDefault();
    if (!canSaveInterval || saving) return;
    saving = true;
    if (await setOtaCheckInterval(checkInterval!)) checkIntervalEdit = undefined;
    saving = false;
  }

  async function setChannel(channel: OtaUpdateChannel) {
    if (saving || channel === otaConfig?.updateChannel) return;
    saving = true;
    await setOtaChannel(channel);
    saving = false;
  }

  async function checkForUpdates() {
    if (saving) return;
    saving = true;
    await checkOtaUpdates(otaConfig?.updateChannel ?? OtaUpdateChannel.Stable);
    saving = false;
  }
</script>

<div class="flex flex-col gap-4">
  <SectionHeader title="OTA Updates" description="Over-the-air firmware update settings." />

  <SettingSwitch
    id="ota-enabled"
    class="rounded-lg border p-3"
    label="OTA Updates Enabled"
    description="Allow the hub to check for and install firmware updates."
    checked={otaConfig?.isEnabled ?? false}
    onchange={otaConfig ? setOtaEnabled : undefined}
  />

  <div class="flex flex-col gap-2">
    <Label for="ota-domain">CDN Domain</Label>
    <Input id="ota-domain" type="text" value={otaConfig?.cdnDomain ?? ''} readonly />
    <p class="text-muted-foreground text-xs">
      Firmware is downloaded from this domain, so it can only be changed over the serial console.
    </p>
  </div>

  <div class="flex flex-col gap-2">
    <Label>Update Channel</Label>
    <div class="flex gap-2" role="group" aria-label="Update channel">
      {#each channels as channel (channel.value)}
        {@const active = otaConfig?.updateChannel === channel.value}
        <Button
          size="sm"
          variant={active ? 'default' : 'outline'}
          aria-pressed={active}
          disabled={!otaConfig || saving}
          onclick={() => setChannel(channel.value)}
        >
          {channel.label}
        </Button>
      {/each}
    </div>
  </div>

  <div class="flex flex-col gap-2">
    <Label for="ota-interval">Check Interval (minutes)</Label>
    <form class="flex gap-2" onsubmit={saveCheckInterval}>
      <Input
        id="ota-interval"
        type="number"
        min={0}
        max={65535}
        step={1}
        aria-invalid={!intervalValid}
        bind:value={() => checkInterval, (v) => (checkIntervalEdit = v)}
      />
      <Button size="sm" variant="outline" type="submit" disabled={!canSaveInterval || saving}>
        Save
      </Button>
    </form>
  </div>

  <SettingSwitch
    id="ota-backend-management"
    class="rounded-lg border p-3"
    label="Allow Backend Management"
    description="Let the gateway server trigger updates."
    checked={otaConfig?.allowBackendManagement ?? false}
    onchange={otaConfig ? setOtaAllowBackendManagement : undefined}
  />

  <SettingSwitch
    id="ota-manual-approval"
    class="rounded-lg border p-3"
    label="Require Manual Approval"
    description="Prompt before installing updates."
    checked={otaConfig?.requireManualApproval ?? false}
    onchange={otaConfig ? setOtaRequireManualApproval : undefined}
  />

  <Button onclick={checkForUpdates} disabled={!otaConfig || saving}>Check for Updates</Button>
</div>
