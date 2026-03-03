<script lang="ts">
  import { cn } from "$lib/utils.js";
  import {
    StepperRootState,
    setStepperRootContext,
  } from "./stepper.svelte.js";
  import type { StepperRootProps } from "./types.js";

  let {
    value = $bindable(1),
    onValueChange,
    orientation = "horizontal",
    linear = false,
    class: className,
    children,
    ...restProps
  }: StepperRootProps = $props();

  const state = new StepperRootState({
    value: {
      get current() {
        return value!;
      },
      set current(v: number) {
        value = v;
        onValueChange?.(v);
      },
    },
    orientation,
    linear,
  });

  setStepperRootContext(state);
</script>

<div
  data-orientation={orientation}
  data-linear={linear}
  class={cn("flex flex-col gap-6", className)}
  {...restProps}
>
  {@render children?.()}
</div>
