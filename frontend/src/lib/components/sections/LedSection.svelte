<script lang="ts">
  import ColorWheel from '#lib/components/ColorWheel.svelte';
  import SectionHeader from '#lib/components/SectionHeader.svelte';
  import {
    fetchLedSettings,
    previewLedColor,
    previewLedPattern,
    setLedSettings,
    stopLedPreview,
    type LedMode,
    type LedPattern,
    type LedSettings,
    type LedSignal,
    type LedType,
  } from '#lib/api.js';
  import { Button } from '@openshock/svelte-core/components/ui/button';
  import { Label } from '@openshock/svelte-core/components/ui/label';
  import { Slider } from '@openshock/svelte-core/components/ui/slider';
  import { TriangleAlert } from '@lucide/svelte';
  import { onDestroy, onMount } from 'svelte';

  const modes: { value: LedMode; label: string; description: string }[] = [
    { value: 'all', label: 'All', description: 'Show every state.' },
    {
      value: 'critical',
      label: 'Critical only',
      description: 'Dark during normal operation; only an E-Stop or a critical error lights up.',
    },
    { value: 'off', label: 'Off', description: 'Always dark, even for an E-Stop or an error.' },
  ];

  const patterns: { value: LedPattern; label: string }[] = [
    { value: 'connected', label: 'Connected' },
    { value: 'nobackend', label: 'No server' },
    { value: 'disconnected', label: 'WiFi disconnected' },
    { value: 'scanning', label: 'WiFi scanning' },
    { value: 'estop', label: 'E-Stop' },
    { value: 'estopclearing', label: 'E-Stop clearing' },
    { value: 'estoprelease', label: 'E-Stop release' },
    { value: 'error', label: 'Critical error' },
  ];

  const ledTypes: { type: LedType; label: string }[] = [
    { type: 'mono', label: 'Built-in LED' },
    { type: 'rgb', label: 'RGB LED' },
  ];

  const signals: { signal: LedSignal; label: string; description: string; color: string }[] = [
    { signal: 'critical', label: 'Critical', description: 'E-Stop and errors', color: 'ff0000' },
    { signal: 'normal', label: 'Normal', description: 'Everything else', color: '00ff00' },
  ];

  let settings = $state<LedSettings | null>(null);
  let saving = $state(false);
  let previewing = $state(false);
  let color = $state('ff0000');

  onMount(async () => {
    settings = await fetchLedSettings();
  });

  // Hand the LEDs back when leaving the section rather than waiting for the preview to time out
  onDestroy(() => {
    if (previewing) void stopLedPreview();
  });

  function settingKey(type: LedType, signal: LedSignal) {
    return `${type}${signal === 'critical' ? 'Critical' : 'Normal'}` as const;
  }

  // Sends at most one request at a time; while one is in flight only the latest value waits. A dragged slider or the
  // color wheel fire far faster than the hub can answer.
  function latestOnly<T>(send: (value: T) => Promise<unknown>) {
    let busy = false;
    let pending: { value: T } | null = null;
    return (value: T) => {
      pending = { value };
      if (busy) return;
      busy = true;
      void (async () => {
        while (pending) {
          const next = pending.value;
          pending = null;
          await send(next);
        }
        busy = false;
      })();
    };
  }

  const sendColorPreview = latestOnly(
    async (p: { color: string; led?: LedType; brightness?: number }) => {
      if (await previewLedColor(p.color, { led: p.led, brightness: p.brightness }))
        previewing = true;
    }
  );

  async function setMode(mode: LedMode) {
    if (!settings || saving || mode === settings.mode) return;
    saving = true;
    if (await setLedSettings({ mode })) settings.mode = mode;
    saving = false;
  }

  // While dragging: preview that LED alone, in the signal's color, at the slider's brightness. Saved on release.
  function onBrightnessDrag(type: LedType, signal: LedSignal, value: number) {
    if (!settings) return;
    settings[type][signal] = value;
    const signalColor = signals.find((s) => s.signal === signal)!.color;
    sendColorPreview({ color: signalColor, led: type, brightness: value });
  }

  async function onBrightnessCommit(type: LedType, signal: LedSignal, value: number) {
    if (!settings) return;
    saving = true;
    await setLedSettings({ [settingKey(type, signal)]: String(value) });
    saving = false;
  }

  async function previewPattern(pattern: LedPattern) {
    await previewLedPattern(pattern);
    previewing = true;
  }

  function onColorChange(value: string) {
    sendColorPreview({ color: value });
  }

  async function stopPreview() {
    await stopLedPreview();
    previewing = false;
  }

  let presentTypes = $derived(settings ? ledTypes.filter((t) => settings![t.type].present) : []);
  let previewSeconds = $derived(Math.round((settings?.previewMs ?? 10000) / 1000));
</script>

<div class="flex flex-col gap-6">
  <SectionHeader title="Status LEDs" description="What the LEDs show, and how bright they are." />

  {#if !settings}
    <p class="text-muted-foreground text-sm">Loading LED settings…</p>
  {:else if presentTypes.length === 0}
    <p class="text-muted-foreground text-sm">This board has no status LED.</p>
  {:else}
    <div class="flex flex-col gap-2">
      <Label>Mode</Label>
      <div class="flex flex-wrap gap-2" role="group" aria-label="LED mode">
        {#each modes as mode (mode.value)}
          {@const active = settings.mode === mode.value}
          <Button
            size="sm"
            variant={active ? 'default' : 'outline'}
            aria-pressed={active}
            disabled={saving}
            onclick={() => setMode(mode.value)}
          >
            {mode.label}
          </Button>
        {/each}
      </div>
      <p class="text-muted-foreground text-xs">
        {modes.find((m) => m.value === settings!.mode)?.description}
      </p>
    </div>

    {#each presentTypes as { type, label } (type)}
      <div class="flex flex-col gap-4 rounded-lg border p-3">
        <p class="text-sm font-medium">{label} brightness</p>
        {#each signals as s (s.signal)}
          {@const value = settings[type][s.signal]}
          <div class="flex flex-col gap-2">
            <div class="flex items-baseline justify-between gap-2">
              <Label for="led-{type}-{s.signal}">
                {s.label}
                <span class="text-muted-foreground font-normal">({s.description})</span>
              </Label>
              <span class="text-sm tabular-nums">{value}%</span>
            </div>
            <Slider
              id="led-{type}-{s.signal}"
              type="single"
              min={0}
              max={100}
              step={1}
              {value}
              onValueChange={(v: number) => onBrightnessDrag(type, s.signal, v)}
              onValueCommit={(v: number) => onBrightnessCommit(type, s.signal, v)}
            />
            {#if type === 'rgb' && value > settings.rgbWarnAbove}
              <p class="flex items-center gap-1.5 text-xs text-amber-600 dark:text-amber-400">
                <TriangleAlert class="h-3.5 w-3.5 shrink-0" />
                Above {settings.rgbWarnAbove}% the RGB LED is glaring and runs warm.
              </p>
            {/if}
          </div>
        {/each}
      </div>
    {/each}

    <div class="flex flex-col gap-2">
      <Label>Test patterns</Label>
      <p class="text-muted-foreground text-xs">
        Shows a state's pattern whatever the mode, at the saved brightness, for {previewSeconds} seconds.
      </p>
      <div class="grid grid-cols-2 gap-2 sm:grid-cols-4">
        {#each patterns as pattern (pattern.value)}
          <Button size="sm" variant="outline" onclick={() => previewPattern(pattern.value)}>
            {pattern.label}
          </Button>
        {/each}
      </div>
    </div>

    {#if settings.rgb.present}
      <div class="flex flex-col gap-2">
        <Label>Test a color</Label>
        <p class="text-muted-foreground text-xs">
          Lights the RGB LED in the picked color at its normal brightness.
        </p>
        <div class="flex items-center gap-4">
          <ColorWheel bind:color size={160} onchange={onColorChange} />
          <div class="flex flex-col gap-2">
            <span
              class="size-10 rounded-md border"
              style:background="#{color}"
              aria-label="Picked color #{color}"
            ></span>
            <span class="text-muted-foreground font-mono text-xs">#{color}</span>
          </div>
        </div>
      </div>
    {/if}

    <Button variant="outline" disabled={!previewing} onclick={stopPreview}>Stop preview</Button>
  {/if}
</div>
