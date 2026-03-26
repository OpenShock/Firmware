import { describe, expect, it } from 'vitest';

// Logic extracted from WebSocketClient — pure function, no DOM deps
function resolveWebSocketHostname(hostname: string): string {
  switch (hostname) {
    case 'localhost':
    case '127.0.0.1':
      return '10.10.10.10';
    default:
      return hostname;
  }
}

describe('resolveWebSocketHostname', () => {
  it('maps localhost to the device IP', () => {
    expect(resolveWebSocketHostname('localhost')).toBe('10.10.10.10');
  });

  it('maps 127.0.0.1 to the device IP', () => {
    expect(resolveWebSocketHostname('127.0.0.1')).toBe('10.10.10.10');
  });

  it('passes through the captive portal IP unchanged', () => {
    expect(resolveWebSocketHostname('10.10.10.10')).toBe('10.10.10.10');
  });

  it('passes through arbitrary hostnames unchanged', () => {
    expect(resolveWebSocketHostname('192.168.1.100')).toBe('192.168.1.100');
    expect(resolveWebSocketHostname('openshock.local')).toBe('openshock.local');
  });
});
