<script lang="ts">
  import { cn } from "$lib/utils/shadcn.js";
  import { getStepperItemContext } from "./stepper.svelte.js";
  import type { StepperDescriptionProps } from "./types.js";

  let {
    class: className,
    children,
    ...restProps
  }: StepperDescriptionProps = $props();

  const item = getStepperItemContext();
</script>

<p
  data-state={item.state}
  class={cn(
    "text-muted-foreground text-xs",
    className,
  )}
  {...restProps}
>
  {@render children?.()}
</p>
