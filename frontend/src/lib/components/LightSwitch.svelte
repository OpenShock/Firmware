<script lang="ts">
  import { buttonVariants } from '$lib/components/ui/button';
  import * as DropdownMenu from '$lib/components/ui/dropdown-menu';
  import { colorScheme, ColorScheme } from '$lib/stores';

  import { Moon, Sun } from '@lucide/svelte';
</script>

<DropdownMenu.Root>
  <DropdownMenu.Trigger class={buttonVariants({ variant: 'outline', size: 'icon' })}>
    <Sun class="size-[1.2rem] scale-100 rotate-0 transition-all dark:scale-0 dark:-rotate-90" />
    <Moon
      class="absolute size-[1.2rem] scale-0 rotate-90 transition-all dark:scale-100 dark:rotate-0"
    />
    <span class="sr-only">Toggle theme</span>
  </DropdownMenu.Trigger>
  <DropdownMenu.Content align="end">
    <DropdownMenu.Item onclick={() => (colorScheme.Value = ColorScheme.Light)}>Light</DropdownMenu.Item>
    <DropdownMenu.Item onclick={() => (colorScheme.Value = ColorScheme.Dark)}>Dark</DropdownMenu.Item>
    <DropdownMenu.Item onclick={() => (colorScheme.Value = ColorScheme.System)}>System</DropdownMenu.Item>
  </DropdownMenu.Content>
</DropdownMenu.Root>
