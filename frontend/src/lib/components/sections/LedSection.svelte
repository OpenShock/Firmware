<script lang="ts">
  import ColorWheel from '#lib/components/ColorWheel.svelte';
  import LabeledSlider from '#lib/components/LabeledSlider.svelte';
  import SectionHeader from '#lib/components/SectionHeader.svelte';
  import { setLedSettings } from '#lib/api.js';
  import {
    LedPreviewPattern,
    LedTarget,
    hexToRgb,
    previewLedColor,
    previewLedPattern,
    stopLedPreview,
    type Rgb,
  } from '#lib/ledPreview.js';
  import type { LedMode } from '#lib/mappers/ConfigMapper.js';
  import { hubState } from '#lib/stores/index.js';
  import { Button } from '@openshock/svelte-core/components/ui/button';
  import { Label } from '@openshock/svelte-core/components/ui/label';
  import { TriangleAlert } from '@lucide/svelte';
  import { onDestroy } from 'svelte';
  import { toast } from 'svelte-sonner';

  type LedType = 'mono' | 'rgb';
  type LedSignal = 'critical' | 'normal';

  const modes: { value: LedMode; label: string; description: string }[] = [
    { value: 'all', label: 'All', description: 'Show every state.' },
    {
      value: 'critical',
      label: 'Critical Only',
      description: 'Dark during normal operation; only an E-Stop or a critical error lights up.',
    },
    { value: 'off', label: 'Off', description: 'Always dark, even for an E-Stop or an error.' },
  ];

  const patterns: { value: LedPreviewPattern; label: string }[] = [
    { value: LedPreviewPattern.Connected, label: 'Connected' },
    { value: LedPreviewPattern.NoBackend, label: 'No Server' },
    { value: LedPreviewPattern.WifiDisconnected, label: 'WiFi Disconnected' },
    { value: LedPreviewPattern.WifiScanning, label: 'WiFi Scanning' },
    { value: LedPreviewPattern.EStopped, label: 'E-Stop' },
    { value: LedPreviewPattern.EStopClearing, label: 'E-Stop Clearing' },
    { value: LedPreviewPattern.EStopAwaitingRelease, label: 'E-Stop Release' },
    { value: LedPreviewPattern.CriticalError, label: 'Critical Error' },
  ];

  const ledTypes: { type: LedType; label: string; target: LedTarget }[] = [
    { type: 'mono', label: 'Built-in LED', target: LedTarget.Mono },
    { type: 'rgb', label: 'RGB LED', target: LedTarget.Rgb },
  ];

  const signals: { signal: LedSignal; label: string; description: string; color: string }[] = [
    { signal: 'critical', label: 'Critical', description: 'E-Stop and errors', color: 'ff0000' },
    { signal: 'normal', label: 'Normal', description: 'Everything else', color: '00ff00' },
  ];

  let led = $derived(hubState.config?.led);

  // A brightness being dragged, shown instead of the saved value until it is saved
  let dragging = $state<{ type: LedType; signal: LedSignal; value: number } | null>(null);
  let saving = $state(false);
  let previewing = $state(false);
  let color = $state('ff0000');

  // Hand the LEDs back when leaving the section rather than waiting for the preview to time out
  onDestroy(() => {
    if (saveTimer !== null) void saveBrightness();
    if (previewing) stopLedPreview();
  });

  function brightness(type: LedType, signal: LedSignal): number {
    if (dragging && dragging.type === type && dragging.signal === signal) return dragging.value;
    return led?.[type][signal] ?? 0;
  }

  function settingKey(type: LedType, signal: LedSignal) {
    return `${type}${signal === 'critical' ? 'Critical' : 'Normal'}` as const;
  }

  function notConnected() {
    toast.error('Not connected to the hub, the preview was not sent');
  }

  async function setMode(mode: LedMode) {
    if (!led || saving || mode === led.mode) return;
    saving = true;
    await setLedSettings({ mode }, (l) => (l.mode = mode));
    saving = false;
  }

  // While dragging: light that LED alone, in the signal's color, at the slider's brightness. Saved on release.
  function onBrightnessDrag(type: LedType, signal: LedSignal, value: number) {
    // Another slider's unsaved value is saved first, or it would snap back to the saved one
    if (saveTimer !== null && dragging && (dragging.type !== type || dragging.signal !== signal)) {
      void saveBrightness();
    }
    dragging = { type, signal, value };
    const target = ledTypes.find((t) => t.type === type)!.target;
    const signalColor = signals.find((s) => s.signal === signal)!.color;
    if (previewLedColor({ color: hexToRgb(signalColor), target, brightness: value }))
      previewing = true;
  }

  // A release, but also every arrow-key press, commits; saving waits for them to stop so holding an arrow key is one
  // flash write, not one per step
  const SAVE_DELAY_MS = 500;
  let saveTimer: ReturnType<typeof setTimeout> | null = null;

  function onBrightnessCommit(type: LedType, signal: LedSignal, value: number) {
    dragging = { type, signal, value };
    if (saveTimer !== null) clearTimeout(saveTimer);
    saveTimer = setTimeout(() => void saveBrightness(), SAVE_DELAY_MS);
  }

  async function saveBrightness() {
    if (saveTimer !== null) {
      clearTimeout(saveTimer);
      saveTimer = null;
    }
    if (!dragging) return;
    const { type, signal, value } = dragging;
    saving = true;
    await setLedSettings({ [settingKey(type, signal)]: String(value) }, (l) => {
      l[type][signal] = value;
    });
    if (dragging?.type === type && dragging.signal === signal && dragging.value === value)
      dragging = null;
    saving = false;
  }

  function previewPattern(pattern: LedPreviewPattern) {
    if (!previewLedPattern(pattern)) return notConnected();
    previewing = true;
  }

  function onColorChange(value: Rgb) {
    if (!previewLedColor({ color: value })) return notConnected();
    previewing = true;
  }

  function stopPreview() {
    stopLedPreview();
    previewing = false;
  }

  let presentTypes = $derived(led ? ledTypes.filter((t) => led[t.type].present) : []);
  let previewSeconds = $derived(Math.round((led?.previewMs ?? 10000) / 1000));
</script>

<div class="flex flex-col gap-4">
  <SectionHeader title="Status LEDs" description="What the LEDs show, and how bright they are." />

  {#if !led}
    <p class="text-muted-foreground text-sm">Loading...</p>
  {:else if presentTypes.length === 0}
    <p class="text-muted-foreground text-sm">This board has no status LED.</p>
  {:else}
    <div class="flex flex-col gap-2">
      <Label>Mode</Label>
      <div class="flex flex-wrap gap-2" role="group" aria-label="LED mode">
        {#each modes as mode (mode.value)}
          {@const active = led.mode === mode.value}
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
        {modes.find((m) => m.value === led.mode)?.description}
      </p>
    </div>

    {#each presentTypes as { type, label } (type)}
      <div class="flex flex-col gap-4 rounded-lg border p-3">
        <h2 class="text-sm font-medium">{label} Brightness</h2>
        {#each signals as s (s.signal)}
          {@const value = brightness(type, s.signal)}
          {@const labelId = `led-${type}-${s.signal}-label`}
          <div class="flex flex-col gap-2">
            <div class="flex items-baseline justify-between gap-2">
              <span id={labelId} class="text-sm font-medium">
                {s.label}
                <span class="text-muted-foreground font-normal">({s.description})</span>
              </span>
              <span class="text-sm tabular-nums" aria-hidden="true">{value}%</span>
            </div>
            <LabeledSlider
              labelledby={labelId}
              {value}
              valuetext="{value}%"
              onValueChange={(v) => onBrightnessDrag(type, s.signal, v)}
              onValueCommit={(v) => onBrightnessCommit(type, s.signal, v)}
            />
            {#if type === 'rgb' && value > led.rgbWarnAbove}
              <p class="text-warning flex items-center gap-1.5 text-xs">
                <TriangleAlert class="h-3.5 w-3.5 shrink-0" />
                Above {led.rgbWarnAbove}% the RGB LED is glaring and runs warm.
              </p>
            {/if}
          </div>
        {/each}
      </div>
    {/each}

    <div class="flex flex-col gap-2">
      <Label>Test Patterns</Label>
      <div class="grid grid-cols-2 gap-2 sm:grid-cols-4">
        {#each patterns as pattern (pattern.value)}
          <Button size="sm" variant="outline" onclick={() => previewPattern(pattern.value)}>
            {pattern.label}
          </Button>
        {/each}
      </div>
    </div>

    {#if led.rgb.present}
      <div class="flex flex-col gap-2">
        <Label>Test Color</Label>
        <div class="flex items-center gap-4">
          <ColorWheel bind:color size={160} onchange={onColorChange} />
          <div class="flex flex-col gap-2">
            <span class="size-10 rounded-md border" style:background={'#' + color}></span>
            <span class="text-muted-foreground font-mono text-xs">#{color}</span>
          </div>
        </div>
      </div>
    {/if}

    <div class="border-info/30 bg-info/10 rounded-lg border p-3">
      <p class="text-xs">
        Previews show a state's pattern whatever the mode, at the saved brightness{led.rgb.present
          ? ', or light the RGB LED in the picked color'
          : ''}. They last {previewSeconds} seconds and change nothing on the hub.
      </p>
    </div>

    <Button variant="outline" disabled={!previewing} onclick={stopPreview}>Stop Preview</Button>
  {/if}
</div>
