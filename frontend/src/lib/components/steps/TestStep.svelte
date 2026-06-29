<script lang="ts">
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { Button } from '$lib/components/ui/button';
  import { Input } from '$lib/components/ui/input';
  import { Label } from '$lib/components/ui/label';
  import { Zap } from '@lucide/svelte';
  import { Builder as FlatbufferBuilder } from 'flatbuffers';
  import { LocalToHubMessage } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message';
  import { LocalToHubMessagePayload } from '$lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload';
  import { ShockerCommandList } from '$lib/_fbs/open-shock/serialization/common/shocker-command-list';
  import { ShockerCommand } from '$lib/_fbs/open-shock/serialization/common/shocker-command';
  import { ShockerModelType } from '$lib/_fbs/open-shock/serialization/types/shocker-model-type';
  import { ShockerCommandType } from '$lib/_fbs/open-shock/serialization/types/shocker-command-type';

  const modelOptions = [
    { value: ShockerModelType.CaiXianlin, label: 'CaiXianlin' },
    { value: ShockerModelType.Petrainer, label: 'Petrainer' },
    { value: ShockerModelType.Petrainer998DR, label: 'Petrainer 998DR' },
    { value: ShockerModelType.WellturnT330, label: 'Wellturn T330' },
  ];

  let shockerId = $state(12345);
  let model = $state(ShockerModelType.CaiXianlin);
  let testing = $state(false);
  let validId = $derived(shockerId >= 0 && shockerId <= 65535);

  function sendTestVibrate() {
    if (!validId) return;
    testing = true;

    const fbb = new FlatbufferBuilder(128);

    const cmdOffset = ShockerCommand.createShockerCommand(fbb, model, shockerId, ShockerCommandType.Vibrate, 50, 1000);
    const cmdsVector = ShockerCommandList.createCommandsVector(fbb, [cmdOffset]);
    const listOffset = ShockerCommandList.createShockerCommandList(fbb, cmdsVector);

    const msgOffset = LocalToHubMessage.createLocalToHubMessage(
      fbb,
      LocalToHubMessagePayload.Common_ShockerCommandList,
      listOffset,
    );

    fbb.finish(msgOffset);
    WebSocketClient.Instance.Send(fbb.asUint8Array());

    setTimeout(() => (testing = false), 1500);
  }
</script>

<div class="flex flex-col gap-4">
  <div>
    <h3 class="text-lg font-semibold">Test Shocker</h3>
    <p class="text-muted-foreground text-sm">
      Verify your shocker is working by sending a test vibration.
    </p>
  </div>

  <div class="flex flex-col gap-3">
    <div class="flex flex-row items-center gap-4">
      <Label for="shocker-model" class="w-20 text-right">Model</Label>
      <select
        id="shocker-model"
        class="border-input bg-background ring-offset-background focus-visible:ring-ring flex h-10 w-full flex-1 rounded-md border px-3 py-2 text-sm focus-visible:ring-2 focus-visible:ring-offset-2 focus-visible:outline-none"
        bind:value={model}
      >
        {#each modelOptions as opt}
          <option value={opt.value}>{opt.label}</option>
        {/each}
      </select>
    </div>

    <div class="flex flex-row items-center gap-4">
      <Label for="shocker-id" class="w-20 text-right">ID</Label>
      <Input
        id="shocker-id"
        type="number"
        min={0}
        max={65535}
        class="flex-1"
        bind:value={shockerId}
        onblur={() => { shockerId = Math.max(0, Math.min(65535, Math.floor(shockerId))); }}
      />
    </div>
  </div>

  <Button onclick={sendTestVibrate} disabled={testing || !validId} class="w-full">
    <Zap class="mr-2 h-4 w-4" />
    {testing ? 'Testing...' : 'Test Vibrate (50%)'}
  </Button>

  <div class="rounded-lg border border-blue-500/30 bg-blue-500/10 p-3">
    <p class="text-xs text-blue-700 dark:text-blue-300">
      If the shocker doesn't respond, try re-pairing it: hold the power button on the shocker
      until it beeps, then press Test again.
    </p>
  </div>
</div>
