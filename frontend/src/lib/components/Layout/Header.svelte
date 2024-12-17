<script lang="ts">
  import LightSwitch from '$lib/components/LightSwitch.svelte';
</script>

<header
  class="sticky top-0 z-50 flex w-full flex-row border-b border-border/40 bg-background/95 backdrop-blur supports-[backdrop-filter]:bg-background/60"
>
  <div class="flex items-center space-x-4">
    <!-- Logo -->
    <a
      href="/"
      class="select-none overflow-hidden lg:!ml-0 lg:w-auto"
      data-sveltekit-preload-data="hover"
    >
      <img class="pointer-events-none inline-block h-12" src="/logo.svg" alt="OpenShock Logo" />
    </a>
  </div>
  <div class="flex-1"></div>
  <div>
    <LightSwitch />
  </div>
</header>
