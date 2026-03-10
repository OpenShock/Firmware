<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { CircleCheck } from '@lucide/svelte';

  interface Props {
    seconds?: number;
    onClose: () => void;
  }

  let { seconds = 5, onClose }: Props = $props();

  let remaining = $state(seconds);
  let interval: ReturnType<typeof setInterval>;

  onMount(() => {
    interval = setInterval(() => {
      remaining -= 1;
      if (remaining <= 0) {
        clearInterval(interval);
        onClose();
      }
    }, 1000);
  });

  onDestroy(() => {
    clearInterval(interval);
  });
</script>

<div class="bg-background fixed inset-0 z-50 flex flex-col items-center justify-center gap-6 p-8">
  <div class="flex flex-col items-center gap-4 text-center">
    <CircleCheck class="h-20 w-20 text-green-500" />
    <h2 class="text-2xl font-bold">Setup Complete!</h2>
    <p class="text-muted-foreground text-sm">
      Your OpenShock device is configured and connected to your account.
    </p>
  </div>

  <div class="flex flex-col items-center gap-2">
    <p class="text-muted-foreground text-sm">
      This portal will close in <span class="font-semibold tabular-nums">{remaining}</span>
      {remaining === 1 ? 'second' : 'seconds'}…
    </p>
    <div class="bg-muted h-2 w-48 overflow-hidden rounded-full">
      <div
        class="h-full rounded-full bg-green-500 transition-all duration-1000"
        style="width: {(remaining / seconds) * 100}%"
      ></div>
    </div>
  </div>
</div>
