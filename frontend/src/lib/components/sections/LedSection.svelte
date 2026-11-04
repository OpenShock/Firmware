<script lang="ts">
  import ColorWheel from '#lib/components/ColorWheel.svelte';
  import LabeledSlider from '#lib/components/LabeledSlider.svelte';
  import SectionHeader from '#lib/components/SectionHeader.svelte';
  import { LedOverrideEnd } from '#lib/_fbs/open-shock/serialization/local/led-override-end.js';
  import { LedRequestError } from '#lib/_fbs/open-shock/serialization/local/led-request-error.js';
  import {
    commitLedDraft,
    LedOverrideLayer,
    LedPattern,
    LedSignal,
    LedTarget,
    onLedNotice,
    previewLedColor,
    previewLedPattern,
    setLedDraft,
    setLedMode,
    stopLedOverride,
    type LedDraft,
  } from '#lib/ledControl.js';
  import type { LedMode } from '#lib/mappers/ConfigMapper.js';
  import type { LedNotice } from '#lib/mappers/LedStatusMapper.js';
  import { hexToRgb, type Rgb } from '#lib/utils/color.js';
  import { hubState } from '#lib/stores/index.js';
  import { Button } from '@openshock/svelte-core/components/ui/button';
  import { Label } from '@openshock/svelte-core/components/ui/label';
  import { Info, LoaderCircle, TriangleAlert } from '@lucide/svelte';
  import { onDestroy } from 'svelte';
  import { toast } from 'svelte-sonner';

  type LedType = 'mono' | 'rgb';
  type SignalName = 'critical' | 'normal';

  const modes: { value: LedMode; label: string; description: string }[] = [
    { value: 'all', label: 'All', description: 'Show every state.' },
    {
      value: 'critical',
      label: 'Critical Only',
      description: 'Dark during normal operation; only an E-Stop or a critical error lights up.',
    },
    { value: 'off', label: 'Off', description: 'Always dark, even for an E-Stop or an error.' },
  ];

  const patterns: { value: LedPattern; label: string }[] = [
    { value: LedPattern.Connected, label: 'Connected' },
    { value: LedPattern.NoBackend, label: 'No Server' },
    { value: LedPattern.WifiDisconnected, label: 'WiFi Disconnected' },
    { value: LedPattern.WifiScanning, label: 'WiFi Scanning' },
    { value: LedPattern.EStopped, label: 'E-Stop' },
    { value: LedPattern.EStopClearing, label: 'E-Stop Clearing' },
    { value: LedPattern.EStopAwaitingRelease, label: 'E-Stop Release' },
    { value: LedPattern.CriticalError, label: 'Critical Error' },
  ];
  const patternLabel = (p: LedPattern) => patterns.find((x) => x.value === p)?.label ?? 'Unknown';

  const ledTypes: { type: LedType; label: string; target: LedTarget }[] = [
    { type: 'mono', label: 'Built-in LED', target: LedTarget.Mono },
    { type: 'rgb', label: 'RGB LED', target: LedTarget.Rgb },
  ];

  const signals: {
    signal: SignalName;
    fbs: LedSignal;
    label: string;
    description: string;
    color: string;
  }[] = [
    {
      signal: 'critical',
      fbs: LedSignal.Critical,
      label: 'Critical',
      description: 'E-Stop and errors',
      color: 'ff0000',
    },
    {
      signal: 'normal',
      fbs: LedSignal.Normal,
      label: 'Normal',
      description: 'Everything else',
      color: '00ff00',
    },
  ];

  const requestErrors: Record<LedRequestError, string> = {
    [LedRequestError.NoLed]: 'this board has no such LED',
    [LedRequestError.LedTestRunning]: 'the LED test is running',
    [LedRequestError.NotHeld]: 'the hub no longer holds it',
    [LedRequestError.SaveFailed]: 'the hub could not save it',
    [LedRequestError.InvalidValue]: 'the hub did not understand the request',
  };

  // How long to wait for the hub to answer a save before saying so. Only an error message hangs on this; what the UI
  // shows always follows the hub.
  const ANSWER_TIMEOUT_MS = 5000;

  // The three sources: `led` is the hub's saved settings (only the hub changes it), `edits` this portal's unsaved
  // brightness, `status` what the hub says the LEDs show and who holds an override. The hub times every override;
  // the countdowns here are only a display of its timeout, and nothing ends on a timer of this page's.
  let led = $derived(hubState.config?.led);
  let status = $derived(hubState.ledStatus);
  let clientId = $derived(hubState.clientId);

  // Brightness is stored as 16 bits (0-65535); the sliders move in tenths of a percent, finer than the RGB LED's
  // 10-bit output is anyway, and an untouched value keeps its exact 16 bits
  const SLIDER_MAX = 1000;
  const toSlider = (b: number) => Math.round((b * SLIDER_MAX) / 65535);
  const fromSlider = (v: number) => Math.round((v * 65535) / SLIDER_MAX);
  const percentText = (b: number) => `${((b * 100) / 65535).toFixed(1)}%`;

  type SettingKey = `${LedType}${'Critical' | 'Normal'}`;
  const settingKey = (type: LedType, signal: SignalName): SettingKey =>
    `${type}${signal === 'critical' ? 'Critical' : 'Normal'}`;

  let edits = $state<Partial<Record<SettingKey, number>>>({});
  let dirty = $derived(Object.keys(edits).length > 0);
  let color = $state('ff0000');

  // Requests waiting on the hub's answer, by seq
  let commit = $state<{ seq: number; saved: Partial<Record<SettingKey, number>> } | null>(null);
  let pendingMode = $state<{ seq: number; mode: LedMode } | null>(null);
  let saving = $derived(commit !== null);

  // When this portal last sent each override, for the countdowns
  let draftSentAt = $state(0);
  let displaySentAt = $state(0);

  // A short message about something the hub did on its own (a revert, another portal taking over)
  let note = $state<{ text: string; tone: 'info' | 'warning' } | null>(null);
  let noteTimer: ReturnType<typeof setTimeout> | null = null;
  function showNote(text: string, tone: 'info' | 'warning' = 'info') {
    note = { text, tone };
    if (noteTimer) clearTimeout(noteTimer);
    noteTimer = setTimeout(() => (note = null), 6000);
  }

  const mine = (o: { owner: number } | null | undefined) =>
    o != null && clientId !== null && o.owner === clientId;
  let myDraft = $derived(mine(status?.draft));
  let myDisplay = $derived(mine(status?.display));
  let otherDraft = $derived(status?.draft != null && !myDraft);
  let otherDisplay = $derived(status?.display != null && !myDisplay);

  // Ticks only while a countdown shows
  let now = $state(Date.now());
  $effect(() => {
    if (!myDraft && !myDisplay) return;
    now = Date.now();
    const id = setInterval(() => (now = Date.now()), 200);
    return () => clearInterval(id);
  });
  const remaining = (sentAt: number, timeoutMs: number) =>
    Math.max(0, Math.ceil((sentAt + timeoutMs - now) / 1000));
  let draftSeconds = $derived(
    myDraft && status?.draft ? remaining(draftSentAt, status.draft.timeoutMs) : null
  );
  let draftFraction = $derived(
    myDraft && status?.draft
      ? Math.max(
          0,
          Math.min(1, (draftSentAt + status.draft.timeoutMs - now) / status.draft.timeoutMs)
        )
      : 0
  );
  let displaySeconds = $derived(
    myDisplay && status?.display ? remaining(displaySentAt, status.display.timeoutMs) : null
  );

  // A reconnect is a new client: the hub dropped the old one's overrides, and its answers won't come
  let lastClientId: number | null = null;
  $effect(() => {
    const id = clientId;
    if (lastClientId !== null && id !== lastClientId) {
      if (Object.keys(edits).length > 0)
        showNote('Reconnected: unsaved brightness was dropped.', 'warning');
      edits = {};
      commit = null;
      pendingMode = null;
    }
    lastClientId = id;
  });

  // The mode is saved when the hub's settings say so
  $effect(() => {
    if (pendingMode && led?.mode === pendingMode.mode) pendingMode = null;
  });

  function draftFromEdits(): LedDraft {
    const draft: LedDraft = {};
    for (const { type } of presentTypes) {
      draft[type] = { critical: brightness(type, 'critical'), normal: brightness(type, 'normal') };
    }
    return draft;
  }

  function handleNotice(n: LedNotice) {
    if (n.owner !== clientId) return;

    if (n.kind === 'failed') {
      const why = requestErrors[n.error] ?? 'unknown error';
      if (commit?.seq === n.seq) {
        commit = null;
        toast.error(`Brightness not saved: ${why}`);
      } else if (pendingMode?.seq === n.seq) {
        pendingMode = null;
        toast.error(`LED mode not saved: ${why}`);
      } else {
        rateLimitedError(`LED preview failed: ${why}`);
      }
      return;
    }

    if (n.layer === LedOverrideLayer.Draft) {
      switch (n.reason) {
        case LedOverrideEnd.Committed: {
          const saved = commit?.seq === n.seq ? commit.saved : {};
          commit = null;
          // Keep anything moved while the save was in flight; it goes back out as a new draft
          for (const [key, value] of Object.entries(saved) as [SettingKey, number][]) {
            if (edits[key] === value) delete edits[key];
          }
          if (Object.keys(edits).length > 0) {
            setLedDraft(draftFromEdits());
            draftSentAt = Date.now();
          }
          toast.success('Brightness saved');
          break;
        }
        case LedOverrideEnd.Expired: {
          const seconds = Math.round((status?.draft?.timeoutMs ?? 10000) / 1000);
          if (Object.keys(edits).length > 0)
            showNote(
              `No changes for ${seconds}s: brightness reverted to the saved values.`,
              'warning'
            );
          edits = {};
          break;
        }
        case LedOverrideEnd.Replaced:
          edits = {};
          showNote(
            'Another portal is adjusting the brightness; your unsaved changes were dropped.',
            'warning'
          );
          break;
        case LedOverrideEnd.LedTest:
          edits = {};
          showNote('The LED test took over the LEDs; unsaved brightness was dropped.', 'warning');
          break;
        default:
          break;
      }
      return;
    }

    switch (n.reason) {
      case LedOverrideEnd.Expired:
        showNote("Preview ended: the LEDs show the hub's state again.");
        break;
      case LedOverrideEnd.Replaced:
        showNote('Another portal started a preview.', 'warning');
        break;
      case LedOverrideEnd.LedTest:
        showNote('The LED test took over the LEDs.', 'warning');
        break;
      default:
        break;
    }
  }

  const unsubscribe = onLedNotice(handleNotice);

  // Hand the LEDs back when leaving the section rather than waiting for the hub to time them out
  onDestroy(() => {
    unsubscribe();
    if (noteTimer) clearTimeout(noteTimer);
    if (myDisplay) stopLedOverride(LedOverrideLayer.Display);
    if (myDraft) stopLedOverride(LedOverrideLayer.Draft);
  });

  // One toast per message every few seconds: a dragged control can fail many times a second
  let lastError = { text: '', at: 0 };
  function rateLimitedError(text: string) {
    const t = Date.now();
    if (text === lastError.text && t - lastError.at < 3000) return;
    lastError = { text, at: t };
    toast.error(text);
  }

  function notConnected() {
    rateLimitedError('Not connected to the hub');
  }

  function brightness(type: LedType, signal: SignalName): number {
    return edits[settingKey(type, signal)] ?? led?.[type][signal] ?? 0;
  }

  // Fails a request with an error if the hub doesn't answer in time
  function expectAnswer(isPending: () => boolean, what: string) {
    setTimeout(() => {
      if (isPending()) toast.error(`No answer from the hub; ${what} may not be saved`);
    }, ANSWER_TIMEOUT_MS);
  }

  function chooseMode(mode: LedMode) {
    if (!led || pendingMode || mode === led.mode) return;
    const seq = setLedMode(mode);
    if (seq === null) return notConnected();
    pendingMode = { seq, mode };
    expectAnswer(() => pendingMode?.seq === seq, 'the LED mode');
  }

  // While moving a slider: the hub shows the draft brightness on whatever the LEDs show, and that LED alone lights in
  // the signal's color at it, so the change is visible whatever state the hub is in
  function onBrightnessChange(type: LedType, signal: SignalName, sliderValue: number) {
    const value = fromSlider(sliderValue);
    const key = settingKey(type, signal);
    if (value === led?.[type][signal]) {
      delete edits[key];
    } else {
      edits[key] = value;
    }

    if (Object.keys(edits).length === 0) {
      if (myDraft) stopLedOverride(LedOverrideLayer.Draft); // Back at the saved values: nothing to hold
    } else if (!setLedDraft(draftFromEdits())) {
      return notConnected();
    }
    draftSentAt = Date.now();

    const s = signals.find((x) => x.signal === signal)!;
    const target = ledTypes.find((t) => t.type === type)!.target;
    if (previewLedColor({ color: hexToRgb(s.color), target, signal: s.fbs }))
      displaySentAt = Date.now();
  }

  function saveBrightness() {
    if (!dirty || saving) return;
    const seq = commitLedDraft();
    if (seq === null) return notConnected();
    commit = { seq, saved: { ...edits } };
    expectAnswer(() => commit?.seq === seq, 'the brightness');
  }

  function discardBrightness() {
    edits = {};
    if (myDraft) stopLedOverride(LedOverrideLayer.Draft);
    if (myDisplay) stopLedOverride(LedOverrideLayer.Display);
  }

  function previewPattern(pattern: LedPattern) {
    if (previewLedPattern(pattern) === null) return notConnected();
    displaySentAt = Date.now();
  }

  function onColorChange(value: Rgb) {
    if (!previewLedColor({ color: value })) return notConnected();
    displaySentAt = Date.now();
  }

  function stopPreview() {
    if (stopLedOverride(LedOverrideLayer.Display) === null) notConnected();
  }

  let presentTypes = $derived(led ? ledTypes.filter((t) => led[t.type].present) : []);

  let showingText = $derived.by(() => {
    if (!status) return null;
    switch (status.showing) {
      case 'live':
        return `${patternLabel(status.pattern)} (the hub's state)`;
      case 'hidden':
        return `Nothing: the mode hides "${patternLabel(status.pattern)}"`;
      case 'pattern':
        return `Preview of "${patternLabel(status.pattern)}"`;
      case 'color':
        return 'Preview of a solid color';
      case 'test':
        return 'The LED test (serial console)';
    }
  });
</script>

<div class="flex flex-col gap-4">
  <SectionHeader title="Status LEDs" description="What the LEDs show, and how bright they are." />

  {#if !led}
    <p class="text-muted-foreground text-sm">Loading...</p>
  {:else if presentTypes.length === 0}
    <p class="text-muted-foreground text-sm">This board has no status LED.</p>
  {:else}
    {#if showingText}
      <div class="flex flex-col gap-1 rounded-lg border p-3" aria-live="polite">
        <p class="text-sm">
          <span class="text-muted-foreground">LEDs now show:</span>
          <span class="font-medium">{showingText}</span>
        </p>
        {#if myDisplay && displaySeconds !== null}
          <p class="text-muted-foreground text-xs">
            Your preview ends in {displaySeconds}s unless you keep interacting.
          </p>
        {:else if otherDisplay}
          <p class="text-muted-foreground text-xs">Another portal is previewing on the LEDs.</p>
        {/if}
        {#if otherDraft}
          <p class="text-muted-foreground text-xs">Another portal is adjusting the brightness.</p>
        {/if}
      </div>
    {/if}

    {#if note}
      <p
        class="flex items-center gap-1.5 text-xs {note.tone === 'warning'
          ? 'text-warning'
          : 'text-muted-foreground'}"
        role="status"
      >
        {#if note.tone === 'warning'}
          <TriangleAlert class="h-3.5 w-3.5 shrink-0" />
        {:else}
          <Info class="h-3.5 w-3.5 shrink-0" />
        {/if}
        {note.text}
      </p>
    {/if}

    <div class="flex flex-col gap-2">
      <Label>Mode</Label>
      <div class="flex flex-wrap gap-2" role="group" aria-label="LED mode">
        {#each modes as mode (mode.value)}
          {@const active = led.mode === mode.value}
          {@const pending = pendingMode?.mode === mode.value}
          <Button
            size="sm"
            variant={active ? 'default' : 'outline'}
            aria-pressed={active}
            aria-busy={pending}
            disabled={pendingMode !== null}
            onclick={() => chooseMode(mode.value)}
          >
            {#if pending}<LoaderCircle class="h-3.5 w-3.5 animate-spin" />{/if}
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
          {@const edited = edits[settingKey(type, s.signal)] !== undefined}
          {@const labelId = `led-${type}-${s.signal}-label`}
          <div class="flex flex-col gap-2">
            <div class="flex items-baseline justify-between gap-2">
              <span id={labelId} class="text-sm font-medium">
                {s.label}
                <span class="text-muted-foreground font-normal">({s.description})</span>
              </span>
              <span class="text-sm tabular-nums" aria-hidden="true">
                {#if edited}
                  <span class="text-muted-foreground text-xs">
                    saved {percentText(led[type][s.signal])} &rarr;
                  </span>
                {/if}
                {percentText(value)}
              </span>
            </div>
            <LabeledSlider
              labelledby={labelId}
              value={toSlider(value)}
              max={SLIDER_MAX}
              valuetext={percentText(value)}
              disabled={saving}
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

    <div class="flex flex-col gap-2">
      <div class="flex gap-2">
        <Button disabled={!dirty || saving} onclick={saveBrightness}>
          {#if saving}<LoaderCircle class="h-4 w-4 animate-spin" />{/if}
          Save Brightness
        </Button>
        <Button variant="outline" disabled={!dirty || saving} onclick={discardBrightness}>
          Discard
        </Button>
      </div>
      {#if dirty && myDraft && draftSeconds !== null}
        <div class="flex flex-col gap-1" aria-live="polite">
          <p class="text-muted-foreground text-xs">
            Unsaved: the LEDs show it, and go back to the saved brightness in {draftSeconds}s unless
            you save or keep adjusting.
          </p>
          <div class="bg-muted h-1 w-full overflow-hidden rounded-full">
            <div
              class="bg-warning h-full transition-[width] duration-200 ease-linear"
              style:width="{draftFraction * 100}%"
            ></div>
          </div>
        </div>
      {:else if dirty && !saving}
        <p class="text-muted-foreground text-xs">Unsaved changes.</p>
      {/if}
    </div>

    <div class="flex flex-col gap-2">
      <Label>Test Patterns</Label>
      <div class="grid grid-cols-2 gap-2 sm:grid-cols-4">
        {#each patterns as pattern (pattern.value)}
          {@const active =
            myDisplay && status?.showing === 'pattern' && status.pattern === pattern.value}
          <Button
            size="sm"
            variant={active ? 'default' : 'outline'}
            aria-pressed={active}
            onclick={() => previewPattern(pattern.value)}
          >
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
            {#if myDisplay && status?.showing === 'color'}
              <span class="text-xs font-medium">On the LED</span>
            {/if}
          </div>
        </div>
      </div>
    {/if}

    <div class="border-info/30 bg-info/10 rounded-lg border p-3">
      <p class="text-xs">
        Previews show a state's pattern whatever the mode{led.rgb.present
          ? ', or light the RGB LED in the picked color'
          : ''}, at the brightness on the sliders. A color preview ends shortly after you stop
        moving it, a pattern after a couple of its cycles. Nothing is saved until you press Save.
      </p>
    </div>

    <Button variant="outline" disabled={!myDisplay} onclick={stopPreview}>Stop Preview</Button>
  {/if}
</div>
