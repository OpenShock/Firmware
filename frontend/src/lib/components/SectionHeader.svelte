<script lang="ts">
  import type { Snippet } from 'svelte';

  interface Props {
    title: string;
    description: string;
    children?: Snippet;
  }

  let { title, description, children }: Props = $props();
</script>

<div class="flex items-center justify-between gap-2">
  <div>
    <h1 class="text-lg font-semibold">{title}</h1>
    <p class="text-muted-foreground text-sm">{description}</p>
  </div>
  {@render children?.()}
</div>
