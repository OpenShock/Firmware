declare global {
  interface Navigator {
    userAgentData?: {
      mobile?: boolean;
    };
  }
  interface ObjectConstructor {
    hasOwn<T extends object, K extends PropertyKey>(o: T, prop: K): o is T & Record<K, unknown>;
  }
}

export {};
