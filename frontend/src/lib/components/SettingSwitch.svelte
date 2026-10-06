<script lang="ts">
  import { Switch } from '@openshock/svelte-core/components/ui/switch';
  import { cn } from '@openshock/svelte-core/utils/shadcn.js';

  interface Props {
    id: string;
    label: string;
    description?: string;
    /** The hub's current value. The switch only moves once this changes. */
    checked: boolean;
    /** Omit to render read-only. */
    onchange?: (checked: boolean) => Promise<unknown>;
    class?: string;
  }

  let { id, label, description, checked, onchange, class: className }: Props = $props();

  let busy = $state(false);

  async function set(value: boolean) {
    if (busy || !onchange) return;
    busy = true;
    try {
      await onchange(value);
    } finally {
      busy = false;
    }
  }
</script>

<div class={cn('flex items-center justify-between gap-4', className)}>
  <label for={id} class="cursor-pointer">
    <span class="block text-sm font-medium">{label}</span>
    {#if description}
      <span class="text-muted-foreground block text-xs">{description}</span>
    {/if}
  </label>
  <Switch {id} bind:checked={() => checked, set} disabled={busy || !onchange} />
</div>
