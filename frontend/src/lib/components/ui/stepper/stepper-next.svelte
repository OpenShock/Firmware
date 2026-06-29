<script lang="ts">
  import { cn } from "$lib/utils/shadcn.js";
  import { buttonVariants } from "$lib/components/ui/button/index.js";
  import { getStepperRootContext } from "./stepper.svelte.js";
  import type { StepperNextProps } from "./types.js";

  let {
    class: className,
    children,
    disabled,
    onclick,
    ...restProps
  }: StepperNextProps = $props();

  const root = getStepperRootContext();

  function handleClick(e: MouseEvent & { currentTarget: HTMLButtonElement }) {
    if (onclick) {
      (onclick as (e: MouseEvent & { currentTarget: HTMLButtonElement }) => void)(e);
    }
    if (!e.defaultPrevented) {
      root.nextStep();
    }
  }
</script>

<button
  type="button"
  disabled={disabled || root.isLastStep}
  onclick={handleClick}
  class={cn(buttonVariants({ variant: "default" }), className)}
  {...restProps}
>
  {#if children}
    {@render children()}
  {:else}
    Next
  {/if}
</button>
