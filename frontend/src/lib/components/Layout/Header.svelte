<script lang="ts">
  import LightSwitch from '$lib/components/LightSwitch.svelte';
</script>

<header
  class="sticky top-0 z-50 w-full flex flex-row border-border/40 bg-background/95 supports-[backdrop-filter]:bg-background/60 border-b backdrop-blur"
>
  <div class="flex items-center space-x-4">
    <!-- Logo -->
    <a
      href="/"
      class="overflow-hidden lg:!ml-0 lg:w-auto select-none"
      data-sveltekit-preload-data="hover"
    >
      <img class="inline-block h-12 pointer-events-none" src="/logo.svg" alt="OpenShock Logo" />
    </a>
  </div>
  <div class="flex-1"></div>
  <div>
    <LightSwitch />
  </div>
</header>
