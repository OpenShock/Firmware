<script lang="ts">
  import ColorWheel from '#lib/components/ColorWheel.svelte';
  import LabeledSlider from '#lib/components/LabeledSlider.svelte';
  import SectionHeader from '#lib/components/SectionHeader.svelte';
  import { setLedSettings } from '#lib/api.js';
  import {
    LedPreviewPattern,
    LedTarget,
    previewLedColor,
    previewLedPattern,
    stopLedPreview,
  } from '#lib/ledPreview.js';
  import type { LedMode } from '#lib/mappers/ConfigMapper.js';
  import { hexToRgb, type Rgb } from '#lib/utils/color.js';
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

  // Brightness is stored as 16 bits (0-65535); the sliders move in tenths of a percent, finer than the RGB LED's
  // 10-bit output is anyway, and an untouched value keeps its exact 16 bits
  const SLIDER_MAX = 1000;
  const toSlider = (b: number) => Math.round((b * SLIDER_MAX) / 65535);
  const fromSlider = (v: number) => Math.round((v * 65535) / SLIDER_MAX);
  const percentText = (b: number) => `${((b * 100) / 65535).toFixed(1)}%`;

  type SettingKey = ReturnType<typeof settingKey>;

  // Brightnesses (0-65535) moved but not saved: shown instead of the saved values, and only previewed on the LEDs until
  // Save writes them all at once
  let edits = $state<Partial<Record<SettingKey, number>>>({});
  let dirty = $derived(Object.keys(edits).length > 0);
  let saving = $state(false);
  let previewing = $state(false);
  let color = $state('ff0000');

  // Hand the LEDs back when leaving the section rather than waiting for the preview to time out. Unsaved brightness
  // changes are dropped.
  onDestroy(() => {
    if (previewing) stopLedPreview();
  });

  function brightness(type: LedType, signal: LedSignal): number {
    return edits[settingKey(type, signal)] ?? led?.[type][signal] ?? 0;
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

  // While moving a slider: light that LED alone, in the signal's color, at the slider's brightness
  function onBrightnessChange(type: LedType, signal: LedSignal, sliderValue: number) {
    const value = fromSlider(sliderValue);
    const key = settingKey(type, signal);
    if (value === led?.[type][signal]) {
      delete edits[key];
    } else {
      edits[key] = value;
    }
    const target = ledTypes.find((t) => t.type === type)!.target;
    const signalColor = signals.find((s) => s.signal === signal)!.color;
    if (previewLedColor({ color: hexToRgb(signalColor), target, brightness: value }))
      previewing = true;
  }

  async function saveBrightness() {
    if (!dirty || saving) return;
    const saved = { ...edits };
    const params = Object.fromEntries(
      Object.entries(saved).map(([key, value]) => [key, String(value)])
    );
    saving = true;
    const ok = await setLedSettings(params, (l) => {
      for (const t of ledTypes) {
        for (const s of signals) {
          const value = saved[settingKey(t.type, s.signal)];
          if (value !== undefined) l[t.type][s.signal] = value;
        }
      }
    });
    saving = false;
    if (!ok) return;
    // Keep anything moved while the save was in flight
    for (const [key, value] of Object.entries(saved) as [SettingKey, number][]) {
      if (edits[key] === value) delete edits[key];
    }
    if (previewing && !dirty) stopPreview(); // Back to the LEDs as they now are
  }

  function discardBrightness() {
    edits = {};
    if (previewing) stopPreview();
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
    if (!stopLedPreview()) {
      toast.error(
        `Not connected to the hub, the preview ends on its own within ${previewSeconds}s`
      );
      return;
    }
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
              <span class="text-sm tabular-nums" aria-hidden="true">{percentText(value)}</span>
            </div>
            <LabeledSlider
              labelledby={labelId}
              value={toSlider(value)}
              max={SLIDER_MAX}
              valuetext={percentText(value)}
              onValueChange={(v) => onBrightnessChange(type, s.signal, v)}
            />
            {#if type === 'rgb' && value > led.rgbWarnAbove}
              <p class="text-warning flex items-center gap-1.5 text-xs">
                <TriangleAlert class="h-3.5 w-3.5 shrink-0" />
                Above {percentText(led.rgbWarnAbove)} the RGB LED is glaring and runs warm.
              </p>
            {/if}
          </div>
        {/each}
      </div>
    {/each}

    <div class="flex gap-2">
      <Button disabled={!dirty || saving} onclick={saveBrightness}>Save Brightness</Button>
      <Button variant="outline" disabled={!dirty || saving} onclick={discardBrightness}>
        Discard
      </Button>
    </div>

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
