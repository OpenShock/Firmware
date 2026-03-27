class UsedPinsStore {
  #pins = $state<Map<number, string>>(new Map());

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
