<script lang="ts">
  import { cn } from "$lib/utils/shadcn.js";
  import {
    getStepperRootContext,
    getStepperItemContext,
  } from "./stepper.svelte.js";
  import type { StepperSeparatorProps } from "./types.js";

  let {
    class: className,
    children,
    ...restProps
  }: StepperSeparatorProps = $props();

  const root = getStepperRootContext();
  const item = getStepperItemContext();
</script>

<div
  data-state={item.state}
  data-orientation={root.orientation}
  class={cn(
    "bg-muted-foreground/40 transition-colors",
    root.orientation === "horizontal" ? "h-0.5 flex-1" : "mx-auto w-0.5 flex-1",
    (item.state === "completed") && "bg-primary",
    className,
  )}
  {...restProps}
>
  {#if children}
    {@render children()}
  {/if}
</div>
