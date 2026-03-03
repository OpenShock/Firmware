<script lang="ts">
  import { cn } from "$lib/utils.js";
  import {
    StepperItemState,
    getStepperRootContext,
    setStepperItemContext,
  } from "./stepper.svelte.js";
  import type { StepperItemProps } from "./types.js";

  let {
    step,
    disabled = false,
    completed = false,
    class: className,
    children,
    ...restProps
  }: StepperItemProps = $props();

  const root = getStepperRootContext();

  // Track total steps
  $effect(() => {
    if (step > root.totalSteps) {
      root.totalSteps = step;
    }
  });

  const itemState = new StepperItemState(root, { step, disabled, completed });

  setStepperItemContext(itemState);
</script>

<div
  data-step={step}
  data-state={itemState.state}
  data-disabled={disabled || undefined}
  data-orientation={root.orientation}
  class={cn(
    "group flex items-center gap-2",
    root.orientation === "vertical" && "flex-col",
    className,
  )}
  {...restProps}
>
  {@render children?.()}
</div>
