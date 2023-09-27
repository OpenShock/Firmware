<script lang="ts">
  import { AppBar, LightSwitch } from '@skeletonlabs/skeleton';
</script>

<AppBar slot="header">
  <div slot="lead" class="flex items-center space-x-4">
    <!-- Logo -->
    <a href="/" class="overflow-hidden lg:!ml-0 lg:w-auto" data-sveltekit-preload-data="hover">
      <img class="inline-block h-12" src="/logo.svg" alt="ShockLink Logo" />
    </a>
  </div>
  <svelte:fragment slot="trail">
    <LightSwitch />
  </svelte:fragment>
</AppBar>
