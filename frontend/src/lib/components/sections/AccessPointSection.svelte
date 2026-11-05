<script lang="ts">
  import SectionHeader from '#lib/components/SectionHeader.svelte';
  import { setAccessPoint, type AccessPointChange } from '#lib/api.js';
  import type { AccessPointProtectionKind } from '#lib/mappers/ConfigMapper.js';
  import { hubState } from '#lib/stores/index.js';
  import { Button } from '@openshock/svelte-core/components/ui/button';
  import { Input } from '@openshock/svelte-core/components/ui/input';
  import { Label } from '@openshock/svelte-core/components/ui/label';
  import { Eye, EyeOff, Info, TriangleAlert } from '@lucide/svelte';
  import { toast } from 'svelte-sonner';

  const protections: { value: AccessPointProtectionKind; label: string; description: string }[] = [
    {
      value: 'default',
      label: 'Default Password',
      description:
        'The password from the setup guide (openshock on official builds). It is public, so it only keeps out passers-by.',
    },
    {
      value: 'custom',
      label: 'Own Password',
      description: 'Only devices that know your password can join and open this portal.',
    },
    {
      value: 'open',
      label: 'Open',
      description: 'No password: anyone nearby can join and open this portal.',
    },
  ];

  let ap = $derived(hubState.config?.accessPoint);

  // Unsaved edits; undefined shows what the hub has saved
  let ssidEdit = $state<string | undefined>();
  let protectionEdit = $state<AccessPointProtectionKind | undefined>();
  let password = $state('');
  let showPassword = $state(false);
  let saving = $state(false);
  let savedOnce = $state(false);

  let ssid = $derived(ssidEdit ?? ap?.ssid ?? '');
  let protection = $derived(protectionEdit ?? ap?.protection ?? 'default');

  // The hub's rules: a name of 1-32 bytes without '|' (empty here means the default), a password of 8-63 printable
  // ASCII characters
  let ssidBytes = $derived(new TextEncoder().encode(ssid.trim()).length);
  let ssidError = $derived(
    ssidBytes > 32 ? 'At most 32 bytes' : ssid.includes('|') ? "No '|' characters" : null
  );

  let ssidChanged = $derived(ap != null && ssid.trim() !== (ap.ssid ?? ''));
  let protectionChanged = $derived(ap != null && protection !== ap.protection);
  // Choosing your own password again with a new one typed in changes it; left empty it keeps the current one
  let passwordChanged = $derived(protection === 'custom' && password.length > 0);
  let passwordNeeded = $derived(
    protection === 'custom' && (protectionChanged || password.length > 0)
  );
  let passwordError = $derived.by(() => {
    if (!passwordNeeded) return null;
    if (password.length < 8) return 'At least 8 characters';
    if (password.length > 63) return 'At most 63 characters';
    if (!/^[\x20-\x7e]*$/.test(password)) return 'Only plain letters, digits, spaces and symbols';
    return null;
  });

  let dirty = $derived(ssidChanged || protectionChanged || passwordChanged);
  let canSave = $derived(dirty && !ssidError && !passwordError && !saving);

  let shownName = $derived(ap ? ap.ssid || ap.defaultSsid : '');

  function discard() {
    ssidEdit = undefined;
    protectionEdit = undefined;
    password = '';
    showPassword = false;
  }

  async function save(e: SubmitEvent) {
    e.preventDefault();
    if (!canSave) return;

    const change: AccessPointChange = {};
    if (ssidChanged) change.ssid = ssid.trim();
    if (protectionChanged || passwordChanged) {
      change.protection = protection;
      if (protection === 'custom') change.password = password;
    }

    saving = true;
    const ok = await setAccessPoint(change);
    saving = false;
    if (!ok) return;

    discard();
    savedOnce = true;
    toast.success('Access point saved; it changes the next time the portal opens');
  }
</script>

<div class="flex flex-col gap-4">
  <SectionHeader
    title="Access Point"
    description="The WiFi network the hub opens for this portal."
  />

  {#if !hubState.config}
    <p class="text-muted-foreground text-sm">Loading...</p>
  {:else if !ap}
    <p class="text-muted-foreground text-sm">
      This firmware can't change the access point from here. Use the <code>ap</code> serial command.
    </p>
  {:else}
    <div class="flex flex-col gap-1 rounded-lg border p-3">
      <p class="text-sm">
        <span class="text-muted-foreground">Saved name:</span>
        <span class="font-medium break-all">{shownName}</span>
        {#if !ap.ssid}<span class="text-muted-foreground">(default)</span>{/if}
      </p>
      <p class="text-sm">
        <span class="text-muted-foreground">Protection:</span>
        <span class="font-medium">{protections.find((p) => p.value === ap.protection)?.label}</span>
      </p>
    </div>

    <form class="flex flex-col gap-4" onsubmit={save}>
      <div class="flex flex-col gap-2">
        <Label for="ap-ssid">Network Name</Label>
        <Input
          id="ap-ssid"
          type="text"
          autocomplete="off"
          spellcheck={false}
          placeholder={ap.defaultSsid}
          aria-invalid={ssidError !== null}
          aria-describedby="ap-ssid-hint"
          bind:value={() => ssid, (v) => (ssidEdit = v)}
        />
        <p
          id="ap-ssid-hint"
          class="text-xs {ssidError ? 'text-destructive' : 'text-muted-foreground'}"
        >
          {ssidError ?? `Leave empty for the default, ${ap.defaultSsid}.`}
        </p>
      </div>

      <div class="flex flex-col gap-2">
        <Label>Password</Label>
        <div class="flex flex-wrap gap-2" role="group" aria-label="Access point protection">
          {#each protections as p (p.value)}
            {@const active = protection === p.value}
            <Button
              size="sm"
              type="button"
              variant={active ? 'default' : 'outline'}
              aria-pressed={active}
              disabled={saving}
              onclick={() => (protectionEdit = p.value)}
            >
              {p.label}
            </Button>
          {/each}
        </div>
        <p class="text-xs {protection === 'open' ? 'text-warning' : 'text-muted-foreground'}">
          {#if protection === 'open'}<TriangleAlert class="mr-1 inline h-3.5 w-3.5" />{/if}
          {protections.find((p) => p.value === protection)?.description}
        </p>

        {#if protection === 'custom'}
          <div class="flex gap-2">
            <Input
              id="ap-password"
              type={showPassword ? 'text' : 'password'}
              autocomplete="new-password"
              class="flex-1"
              placeholder={ap.protection === 'custom' && !protectionChanged
                ? 'Leave empty to keep the current password'
                : '8-63 characters'}
              aria-label="Access point password"
              aria-invalid={passwordError !== null}
              aria-describedby="ap-password-hint"
              bind:value={password}
            />
            <Button
              type="button"
              size="icon"
              variant="outline"
              aria-label={showPassword ? 'Hide password' : 'Show password'}
              aria-pressed={showPassword}
              onclick={() => (showPassword = !showPassword)}
            >
              {#if showPassword}<EyeOff class="h-4 w-4" />{:else}<Eye class="h-4 w-4" />{/if}
            </Button>
          </div>
          {#if passwordError}
            <p id="ap-password-hint" class="text-destructive text-xs">{passwordError}</p>
          {/if}
        {/if}
      </div>

      <div class="flex gap-2">
        <Button type="submit" disabled={!canSave}>Save</Button>
        <Button type="button" variant="outline" disabled={!dirty || saving} onclick={discard}>
          Discard
        </Button>
      </div>
    </form>

    <div class="border-info/30 bg-info/10 flex gap-2 rounded-lg border p-3" role="note">
      <Info class="mt-0.5 h-4 w-4 shrink-0" />
      <p class="text-xs">
        Changes apply the next time the hub opens this portal (after a restart, for one). The
        network you are on now stays as it is until then, so this page keeps working.
        {#if savedOnce}
          <strong>Afterwards, join "{shownName}" with the new password to come back here.</strong>
        {/if}
        Forgot it? A factory reset over USB restores the default name and password.
      </p>
    </div>
  {/if}
</div>
