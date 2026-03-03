<script lang="ts">
  import { cn } from "$lib/utils.js";
  import { getStepperItemContext } from "./stepper.svelte.js";
  import type { StepperIndicatorProps } from "./types.js";

  let {
    class: className,
    children,
    ...restProps
  }: StepperIndicatorProps = $props();

  const item = getStepperItemContext();
</script>

<span
  data-state={item.state}
  class={cn(
    "border-muted-foreground/40 flex h-8 w-8 shrink-0 items-center justify-center rounded-full border-2 text-sm font-medium transition-colors",
    item.state === "active" && "border-primary bg-primary text-primary-foreground",
    item.state === "completed" && "border-primary bg-primary text-primary-foreground",
    className,
  )}
  {...restProps}
>
  {#if children}
    {@render children()}
  {:else if item.state === "completed"}
    <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
  {:else}
    {item.step}
  {/if}
</span>
