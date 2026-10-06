<script lang="ts">
  import { WebSocketClient } from '#lib/WebSocketClient.js';
  import SectionHeader from '#lib/components/SectionHeader.svelte';
  import { Button } from '@openshock/svelte-core/components/ui/button';
  import { Input } from '@openshock/svelte-core/components/ui/input';
  import { Label } from '@openshock/svelte-core/components/ui/label';
  import { Zap } from '@lucide/svelte';
  import { toast } from 'svelte-sonner';
  import { Builder as FlatbufferBuilder } from 'flatbuffers';
  import { LocalToHubMessage } from '#lib/_fbs/open-shock/serialization/local/local-to-hub-message.js';
  import { LocalToHubMessagePayload } from '#lib/_fbs/open-shock/serialization/local/local-to-hub-message-payload.js';
  import { ShockerCommandList } from '#lib/_fbs/open-shock/serialization/common/shocker-command-list.js';
  import { ShockerCommand } from '#lib/_fbs/open-shock/serialization/common/shocker-command.js';
  import { ShockerModelType } from '#lib/_fbs/open-shock/serialization/types/shocker-model-type.js';
  import { ShockerCommandType } from '#lib/_fbs/open-shock/serialization/types/shocker-command-type.js';

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

    const fbb = new FlatbufferBuilder(128);

    const cmdOffset = ShockerCommand.createShockerCommand(
      fbb,
      model,
      shockerId,
      ShockerCommandType.Vibrate,
      50,
      1000
    );
    const cmdsVector = ShockerCommandList.createCommandsVector(fbb, [cmdOffset]);
    const listOffset = ShockerCommandList.createShockerCommandList(fbb, cmdsVector);

    const msgOffset = LocalToHubMessage.createLocalToHubMessage(
      fbb,
      LocalToHubMessagePayload.Common_ShockerCommandList,
      listOffset
    );

    fbb.finish(msgOffset);
    if (!WebSocketClient.Instance.Send(new Uint8Array(fbb.asUint8Array()))) {
      toast.error('Not connected to the hub, test command was not sent');
      return;
    }

    testing = true;
    setTimeout(() => (testing = false), 1500);
  }
</script>

<div class="flex flex-col gap-4">
  <SectionHeader
    title="Test Shocker"
    description="Verify your shocker is working by sending a test vibration."
  />

  <div class="flex flex-col gap-3">
    <div class="flex flex-row items-center gap-4">
      <Label for="shocker-model" class="w-20 text-right">Model</Label>
      <select
        id="shocker-model"
        class="border-input focus-visible:border-ring focus-visible:ring-ring/50 dark:bg-input/30 h-8 w-full flex-1 rounded-lg border bg-transparent px-2.5 text-sm outline-none focus-visible:ring-3"
        bind:value={model}
      >
        {#each modelOptions as opt (opt.value)}
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
        onblur={() => {
          shockerId = Math.max(0, Math.min(65535, Math.floor(shockerId)));
        }}
      />
    </div>
  </div>

  <Button onclick={sendTestVibrate} disabled={testing || !validId} class="w-full">
    <Zap class="mr-2 h-4 w-4" />
    {testing ? 'Testing...' : 'Test Vibrate (50%)'}
  </Button>

  <div class="border-info/30 bg-info/10 rounded-lg border p-3">
    <p class="text-xs">
      If the shocker doesn't respond, try re-pairing it: hold the power button on the shocker until
      it beeps, then press Test again.
    </p>
  </div>
</div>
