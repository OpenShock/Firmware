<script lang="ts">
  import { cn } from "$lib/utils.js";
  import {
    getStepperRootContext,
    getStepperItemContext,
  } from "./stepper.svelte.js";
  import type { StepperTriggerProps } from "./types.js";

  let {
    class: className,
    children,
    disabled,
    ...restProps
  }: StepperTriggerProps = $props();

  const root = getStepperRootContext();
  const item = getStepperItemContext();

  function handleClick() {
    if (item.disabled || disabled) return;
    if (root.linear && item.step > root.value.current) return;
    root.goToStep(item.step);
  }

  let isDisabled = $derived(
    disabled ||
      item.disabled ||
      (root.linear && item.step > root.value.current),
  );
</script>

<button
  type="button"
  role="tab"
  aria-selected={item.isActive}
  data-state={item.state}
  data-disabled={isDisabled || undefined}
  {disabled}
  onclick={handleClick}
  class={cn(
    "inline-flex items-center gap-3 focus-visible:outline-hidden",
    isDisabled && "cursor-not-allowed opacity-50",
    className,
  )}
  {...restProps}
>
  {@render children?.()}
</button>
