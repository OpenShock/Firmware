<script lang="ts">
  import { Button, buttonVariants } from '$lib/components/ui/button';
  import * as Dialog from '$lib/components/ui/dialog';
  import * as DropdownMenu from '$lib/components/ui/dropdown-menu';
  import { colorScheme, ColorScheme, willActivateLightMode, getDarkReaderState } from '$lib/stores';
  import { toast } from 'svelte-sonner';

  import { Moon, Sun } from '@lucide/svelte';

  let pendingScheme = $state<ColorScheme | undefined>();
  function handleOpenChanged(open: boolean) {
    if (open) return;
    pendingScheme = undefined;
  }
  function confirm() {
    if (!pendingScheme) return;
    colorScheme.Value = pendingScheme;
    pendingScheme = undefined;
  }
  function evaluateLightSwitch(scheme: ColorScheme) {
    if (willActivateLightMode(scheme) && scheme !== colorScheme.Value) {
      const darkreader = getDarkReaderState();
      if (darkreader.isActive) {
        toast.warning('DarkReader is enabled, activating light mode will have no effect!');
        return;
      }
      pendingScheme = scheme;
      return;
    }
    colorScheme.Value = scheme;
  }
</script>

<Dialog.Root bind:open={() => pendingScheme !== undefined, handleOpenChanged}>
  <Dialog.Content>
    <Dialog.Header>
      <Dialog.Title>Switch to light mode</Dialog.Title>
      <Dialog.Description>
        <span class="font-bold text-red-500">Warning:</span> You are about to switch to light mode.
        <br />
        Are you sure you want to do this?
      </Dialog.Description>
    </Dialog.Header>
    <Button variant="destructive" onclick={confirm}>I am willing to take the risk</Button>
  </Dialog.Content>
</Dialog.Root>

<DropdownMenu.Root>
  <DropdownMenu.Trigger class={buttonVariants({ variant: 'outline', size: 'icon' })}>
    <Sun class="size-[1.2rem] scale-100 rotate-0 transition-all dark:scale-0 dark:-rotate-90" />
    <Moon
      class="absolute size-[1.2rem] scale-0 rotate-90 transition-all dark:scale-100 dark:rotate-0"
    />
    <span class="sr-only">Toggle theme</span>
  </DropdownMenu.Trigger>
  <DropdownMenu.Content align="end">
    <DropdownMenu.Item onclick={() => evaluateLightSwitch(ColorScheme.Light)}
      >Light</DropdownMenu.Item
    >
    <DropdownMenu.Item onclick={() => evaluateLightSwitch(ColorScheme.Dark)}>Dark</DropdownMenu.Item
    >
    <DropdownMenu.Item onclick={() => evaluateLightSwitch(ColorScheme.System)}
      >System</DropdownMenu.Item
    >
  </DropdownMenu.Content>
</DropdownMenu.Root>
