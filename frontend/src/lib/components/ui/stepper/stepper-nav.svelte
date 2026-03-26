<script lang="ts">
  import { cn } from "$lib/utils/shadcn.js";
  import { getStepperRootContext } from "./stepper.svelte.js";
  import type { StepperNavProps } from "./types.js";

  let { class: className, children, ...restProps }: StepperNavProps = $props();

  const root = getStepperRootContext();
</script>

<div
  role="tablist"
  aria-orientation={root.orientation}
  class={cn(
    "flex items-center gap-2",
    root.orientation === "vertical" && "flex-col",
    className,
  )}
  {...restProps}
>
  {@render children?.()}
</div>
