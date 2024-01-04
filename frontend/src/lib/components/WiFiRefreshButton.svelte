<script lang="ts">
  import { DeviceStateStore } from '$lib/stores';
  import { WebSocketClient } from '$lib/WebSocketClient';
  import { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
  import { SerializeWifiScanCommand } from '$lib/Serializers/WifiScanCommand';

  $: scanStatus = $DeviceStateStore.wifiScanStatus;
  $: isScanning = scanStatus === WifiScanStatus.Started || scanStatus === WifiScanStatus.InProgress;

  function wifiScan() {
    const data = SerializeWifiScanCommand(!isScanning);
    WebSocketClient.Instance.Send(data);
  }
</script>

<button class="btn variant-outline" on:click={wifiScan}>
  {#if isScanning}
    <i class="fa fa-spinner fa-spin"></i>
  {:else}
    <i class="fa fa-rotate-right"></i>
  {/if}
</button>
