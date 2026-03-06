<script lang="ts">
  import { cn } from "$lib/utils/shadcn.js";
  import { buttonVariants } from "$lib/components/ui/button/index.js";
  import { getStepperRootContext } from "./stepper.svelte.js";
  import type { StepperPreviousProps } from "./types.js";

  let {
    class: className,
    children,
    disabled,
    onclick,
    ...restProps
  }: StepperPreviousProps = $props();

  const root = getStepperRootContext();

  function handleClick(e: MouseEvent & { currentTarget: HTMLButtonElement }) {
    if (onclick) {
      (onclick as (e: MouseEvent & { currentTarget: HTMLButtonElement }) => void)(e);
    }
    if (!e.defaultPrevented) {
      root.prevStep();
    }
  }
</script>

<button
  type="button"
  disabled={disabled || root.isFirstStep}
  onclick={handleClick}
  class={cn(buttonVariants({ variant: "outline" }), className)}
  {...restProps}
>
  {#if children}
    {@render children()}
  {:else}
    Back
  {/if}
</button>
