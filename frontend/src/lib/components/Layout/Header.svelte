<script lang="ts">
  import { page } from '$app/stores';
  import { AppBar, LightSwitch } from '@skeletonlabs/skeleton';
</script>

<AppBar slot="header">
  <div slot="lead" class="flex items-center space-x-4">
    <!-- Logo -->
    <a href="/" class="overflow-hidden lg:!ml-0 lg:w-auto select-none" data-sveltekit-preload-data="hover">
      <img class="inline-block h-12 pointer-events-none" src="/logo.svg" alt="OpenShock Logo" />
    </a>
  </div>
  <svelte:fragment slot="trail">
    <!-- Advanced menu -->
    <div class="flex items-center space-x-4">
      {#if $page.url.pathname === '/advanced'}
        <a href="/" class="btn variant-filled">Simple</a>
      {:else}
        <a href="/advanced" class="btn variant-filled">Advanced</a>
      {/if}
    </div>
    <LightSwitch />
  </svelte:fragment>
</AppBar>
