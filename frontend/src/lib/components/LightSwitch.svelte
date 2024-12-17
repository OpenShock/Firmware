<script lang="ts">
  import { buttonVariants } from '$lib/components/ui/button';
  import * as Dialog from '$lib/components/ui/dialog';
  import * as DropdownMenu from '$lib/components/ui/dropdown-menu';
  import { ColorSchemeStore, willActivateLightMode } from '$lib/stores/ColorSchemeStore';
  import AbsolutelySureButton from '$lib/components/AbsolutelySureButton.svelte';

  import { Moon, Sun } from 'lucide-svelte';

  let pendingScheme = $state<'light' | 'dark' | 'system' | undefined>();
  function handleOpenChanged(open: boolean) {
    if (open) return;
    pendingScheme = undefined;
  }
  function confirm() {
    if (!pendingScheme) return;
    ColorSchemeStore.set(pendingScheme);
    pendingScheme = undefined;
  }
  function evaluateLightSwitch(scheme: 'light' | 'dark' | 'system') {
    if (willActivateLightMode(scheme) && scheme !== $ColorSchemeStore) {
      pendingScheme = scheme;
      return;
    }
    ColorSchemeStore.set(scheme);
  }
</script>

<Dialog.Root
  open={pendingScheme !== undefined}
  onOpenChange={handleOpenChanged}
  controlledOpen={true}
>
  <Dialog.Content>
    <Dialog.Header>
      <Dialog.Title>Switch to light mode</Dialog.Title>
      <Dialog.Description>
        <span class="font-bold text-red-500">Warning:</span> You are about to switch to light mode.
        <br />
        Are you sure you want to do this?
      </Dialog.Description>
    </Dialog.Header>
    <AbsolutelySureButton text="I am willing to take the risk" onconfirm={confirm} />
  </Dialog.Content>
</Dialog.Root>

<DropdownMenu.Root>
  <DropdownMenu.Trigger class={buttonVariants({ variant: 'outline', size: 'icon' })}>
    <Sun
      class="h-[1.2rem] w-[1.2rem] rotate-0 scale-100 transition-all dark:-rotate-90 dark:scale-0"
    />
    <Moon
      class="absolute h-[1.2rem] w-[1.2rem] rotate-90 scale-0 transition-all dark:rotate-0 dark:scale-100"
    />
    <span class="sr-only">Toggle theme</span>
  </DropdownMenu.Trigger>
  <DropdownMenu.Content align="end">
    <DropdownMenu.Item onclick={() => evaluateLightSwitch('light')}>Light</DropdownMenu.Item>
    <DropdownMenu.Item onclick={() => evaluateLightSwitch('dark')}>Dark</DropdownMenu.Item>
    <DropdownMenu.Item onclick={() => evaluateLightSwitch('system')}>System</DropdownMenu.Item>
  </DropdownMenu.Content>
</DropdownMenu.Root>
