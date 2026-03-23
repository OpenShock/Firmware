<script lang="ts">
  import { onMount } from 'svelte';
  import { CircleCheck } from '@lucide/svelte';

  interface Props {
    onClose: () => void;
  }

  let { onClose }: Props = $props();

  onMount(() => {
    setTimeout(onClose, 3000);
  });
</script>

<div class="bg-background fixed inset-0 z-50 flex flex-col items-center justify-center gap-6 p-8">
  <div class="flex flex-col items-center gap-4 text-center">
    <CircleCheck class="h-20 w-20 text-green-500" />
    <h2 class="text-2xl font-bold">Setup Complete!</h2>
    <p class="text-muted-foreground text-sm">
      Your OpenShock device is configured and connected to your account.
    </p>
    <p class="text-muted-foreground text-sm">This portal will close shortly.</p>
  </div>
</div>
