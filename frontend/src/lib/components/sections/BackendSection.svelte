<script lang="ts">
  import SectionHeader from '#lib/components/SectionHeader.svelte';
  import { hubState } from '#lib/stores/index.js';
  import { Input } from '@openshock/svelte-core/components/ui/input';
  import { Label } from '@openshock/svelte-core/components/ui/label';

  let domain = $state(hubState.config?.backend?.domain ?? '');

  $effect(() => {
    const d = hubState.config?.backend?.domain;
    if (d) domain = d;
  });
</script>

<div class="flex flex-col gap-4">
  <SectionHeader title="Backend" description="Gateway server configuration." />

  <div class="flex flex-col gap-2">
    <Label for="backend-domain">Domain</Label>
    <Input
      id="backend-domain"
      type="text"
      bind:value={domain}
      disabled
      placeholder="api.openshock.app"
    />
    <p class="text-muted-foreground text-xs">
      The backend domain is read-only here. Use serial commands to change it.
    </p>
  </div>
</div>
