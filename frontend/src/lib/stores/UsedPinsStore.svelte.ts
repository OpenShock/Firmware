import { SvelteMap } from 'svelte/reactivity';

export class UsedPinsStore {
  // $state does not proxy a Map; SvelteMap makes has() reactive for the pin selectors
  #pins = new SvelteMap<number, string>();

  has(pin: number) {
    return this.#pins.has(pin);
  }

  markPinUsed(pin: number, name: string) {
    for (const [key, value] of this.#pins) {
      if (key === pin || value === name) {
        this.#pins.delete(key);
      }
    }
    this.#pins.set(pin, name);
  }
}

export const usedPins = new UsedPinsStore();
