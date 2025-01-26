<script lang="ts">
  import { onDestroy } from 'svelte';

  type Props = {
    text: string;
    onconfirm: () => void;
  };

  let { text, onconfirm }: Props = $props();

  let clickedAt = $state<number | undefined>();
  let buttonText = $state<string>(text);
  function updateText() {
    if (clickedAt === undefined) {
      buttonText = text;
      return;
    }
    const timeLeft = 5 - (Date.now() - clickedAt) / 1000;
    if (timeLeft <= 0) {
      buttonText = text;
      return;
    }
    buttonText = `Hold for ${timeLeft.toFixed(1)}s`;
  }

  let timer = $state<ReturnType<typeof setTimeout> | undefined>();
  let interval = $state<ReturnType<typeof setInterval> | undefined>();
  function stopTimers() {
    if (timer) {
      clearTimeout(timer);
      timer = undefined;
    }
    if (interval) {
      clearInterval(interval);
      interval = undefined;
    }
    clickedAt = undefined;
    updateText();
  }
  function startConfirm() {
    stopTimers();
    clickedAt = Date.now();
    timer = setTimeout(onconfirm, 5000);
    interval = setInterval(updateText, 100);
  }

  onDestroy(stopTimers);
</script>

<button
  onmousedown={startConfirm}
  ontouchstart={startConfirm}
  onmouseup={stopTimers}
  ontouchend={stopTimers}
  onmouseleave={stopTimers}
  class={timer ? 'violent-shake' : undefined}
>
  {buttonText}
</button>

<style lang="postcss">
  button {
    @apply h-10 select-none whitespace-nowrap rounded-md px-4 py-2 text-sm font-medium;
    background: rgb(127, 29, 29);
  }
  button:hover {
    background: rgb(115, 26, 26);
  }
  button:focus {
    outline: none;
  }
  button.violent-shake {
    /* Start the shake animation and make the animation last for 0.5 seconds */
    animation:
      shake 0.5s,
      glow 5s;

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
