<script lang="ts">
  // A single-value slider whose thumb (the element with role="slider") is named by a label element. The shadcn
  // Slider can't do this: it passes extra props to the root, which is not the focusable control, so a <Label for>
  // pointing at it names nothing. Styled like the shadcn Slider (packages/svelte-core .../ui/slider).
  import { Slider as SliderPrimitive } from 'bits-ui';
  import { cn } from '@openshock/svelte-core/utils/shadcn.js';

  interface Props {
    /** The id of the element that names this slider. */
    labelledby: string;
    value: number;
    min?: number;
    max?: number;
    step?: number;
    disabled?: boolean;
    /** Screen-reader text for the value, e.g. "45%". */
    valuetext?: string;
    class?: string;
    onValueChange?: (value: number) => void;
    onValueCommit?: (value: number) => void;
  }

  let {
    labelledby,
    value,
    min = 0,
    max = 100,
    step = 1,
    disabled = false,
    valuetext,
    class: className,
    onValueChange,
    onValueCommit,
  }: Props = $props();
</script>

<SliderPrimitive.Root
  type="single"
  {value}
  {min}
  {max}
  {step}
  {disabled}
  {onValueChange}
  {onValueCommit}
  data-slot="slider"
  class={cn(
    'relative flex w-full touch-none items-center select-none data-disabled:opacity-50',
    className
  )}
>
  {#snippet children({ thumbItems })}
    <span
      data-slot="slider-track"
      class="bg-muted relative h-1 w-full grow overflow-hidden rounded-full"
    >
      <SliderPrimitive.Range
        data-slot="slider-range"
        class="bg-primary absolute h-full select-none"
      />
    </span>
    {#each thumbItems as thumb (thumb.index)}
      <SliderPrimitive.Thumb
        data-slot="slider-thumb"
        index={thumb.index}
        aria-labelledby={labelledby}
        aria-valuetext={valuetext}
        class="border-ring ring-ring/50 relative block size-3 shrink-0 cursor-w-resize rounded-full border bg-white transition-[color,box-shadow] select-none after:absolute after:-inset-2 hover:ring-3 focus-visible:ring-3 focus-visible:outline-hidden active:ring-3 disabled:pointer-events-none disabled:opacity-50"
      />
    {/each}
  {/snippet}
</SliderPrimitive.Root>
