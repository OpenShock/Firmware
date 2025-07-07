<script lang="ts">
  import type { IntervalHandle, TimeoutHandle } from '$lib/types/WAPI';
  import { cn } from '$lib/utils';
  import { onDestroy } from 'svelte';

  interface Props {
    text: string;
    onconfirm: () => void;
  }

  let { text, onconfirm }: Props = $props();

  let clickedAt = $state<number | null>(null);
  let buttonText = $state<string>(text);
  function updateText() {
    if (clickedAt === null) {
      buttonText = text;
      return;
    }
    const timeLeft = 3 - (Date.now() - clickedAt) / 1000;
    if (timeLeft <= 0) {
      buttonText = text;
      return;
    }
    buttonText = `Hold for ${timeLeft.toFixed(1)}s`;
  }

  let timer = $state<TimeoutHandle | null>(null);
  let interval: IntervalHandle | null = null;
  function stopTimers() {
    if (timer) {
      clearTimeout(timer);
      timer = null;
    }
    if (interval) {
      clearInterval(interval);
      interval = null;
    }
    clickedAt = null;
    updateText();
  }
  function startConfirm() {
    stopTimers();
    clickedAt = Date.now();
    timer = setTimeout(onconfirm, 3000);
    interval = setInterval(updateText, 100);
  }

  onDestroy(stopTimers);
</script>

<button
  onpointerdown={startConfirm}
  onpointerup={stopTimers}
  onpointerleave={stopTimers}
  class={cn(
    'h-10 rounded-md bg-[#7f1d1d] px-4 py-2 text-sm font-medium whitespace-nowrap select-none hover:bg-[#731a1a] focus:outline-hidden',
    { 'violent-shake': timer !== null }
  )}
>
  {buttonText}
</button>

<style>
  .violent-shake {
    /* Start the shake animation and make the animation last for 0.5 seconds */
    animation:
      shake 0.5s,
      glow 3s;

    /* When the animation is finished, start again */
    animation-iteration-count: infinite;
  }

  @keyframes shake {
    0% {
      transform: translate(1px, 1px) rotate(0deg);
    }
    10% {
      transform: translate(-1px, -2px) rotate(-1deg);
    }
    20% {
      transform: translate(-3px, 0px) rotate(1deg);
    }
    30% {
      transform: translate(3px, 2px) rotate(0deg);
    }
    40% {
      transform: translate(1px, -1px) rotate(1deg);
    }
    50% {
      transform: translate(-1px, 2px) rotate(-1deg);
    }
    60% {
      transform: translate(-3px, 1px) rotate(0deg);
    }
    70% {
      transform: translate(3px, 1px) rotate(-1deg);
    }
    80% {
      transform: translate(-1px, -1px) rotate(1deg);
    }
    90% {
      transform: translate(1px, 2px) rotate(0deg);
    }
    100% {
      transform: translate(1px, -2px) rotate(-1deg);
    }
  }

  @keyframes glow {
    0% {
      background: rgb(127, 29, 29);
      box-shadow: 0 0 10px 0 rgba(255, 0, 0, 0.5);
    }
    100% {
      background: rgb(255, 29, 29);
      box-shadow: 0 0 50px 10px rgba(255, 0, 0, 0.5);
    }
  }
</style>
