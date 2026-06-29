<script lang="ts">
  import { hubState } from '$lib/stores';
  import { Input } from '$lib/components/ui/input';
  import { Label } from '$lib/components/ui/label';

  let domain = $state(hubState.config?.backend?.domain ?? '');

  $effect(() => {
    const d = hubState.config?.backend?.domain;
    if (d) domain = d;
  });
</script>

<div class="flex flex-col gap-4">
  <div>
    <h3 class="text-lg font-semibold">Backend</h3>
    <p class="text-muted-foreground text-sm">Gateway server configuration.</p>
  </div>

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
