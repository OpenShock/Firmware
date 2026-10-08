import { getDeviceHostname } from '#lib/utils/localRedirect.js';
import { isArrayBuffer, isString } from '@openshock/svelte-core/typeguards';
import { toast } from 'svelte-sonner';
import { WebSocketMessageBinaryHandler } from './MessageHandlers';

export enum ConnectionState {
  DISCONNECTED = 0,
  DISCONNECTING,
  CONNECTING,
  CONNECTED,
}

export type ConnectionStateChangeHandler = (state: ConnectionState) => void;

const RECONNECT_DELAY_MIN_MS = 200;
const RECONNECT_DELAY_MAX_MS = 5000;

export class WebSocketClient {
  public static readonly Instance = new WebSocketClient();

  #socket: WebSocket | null = null;

  #connectionState: ConnectionState = ConnectionState.DISCONNECTED;
  #connectionStateChangeHandlers: ConnectionStateChangeHandler[] = [];
  private set ConnectionState(value: ConnectionState) {
    if (this.#connectionState !== value) {
      this.#connectionState = value;
      this.#connectionStateChangeHandlers.forEach((handler) => handler(value));
    }
  }
  public get ConnectionState(): ConnectionState {
    return this.#connectionState;
  }
  public addConnectionStateChangeHandler(handler: ConnectionStateChangeHandler) {
    this.#connectionStateChangeHandlers.push(handler);
  }
  public removeConnectionStateChangeHandler(handler: ConnectionStateChangeHandler) {
    const index = this.#connectionStateChangeHandlers.indexOf(handler);
    if (index !== -1) {
      this.#connectionStateChangeHandlers.splice(index, 1);
    }
  }

  #autoReconnect = false;
  #reconnectDelayMs = RECONNECT_DELAY_MIN_MS;
  #reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  #abortTimer: ReturnType<typeof setTimeout> | null = null;
  #outageReported = false;

  public Connect() {
    const connectionState = this.ConnectionState;
    if (
      connectionState === ConnectionState.CONNECTED ||
      connectionState === ConnectionState.CONNECTING
    ) {
      return;
    }

    this.clearTimers();
    this.AbortWebSocket();

    this.#autoReconnect = true;
    this.ConnectionState = ConnectionState.CONNECTING;

    const hostname = getDeviceHostname();
    if (!hostname) {
      console.error('[WS] ERROR: Failed to get WebSocket hostname');
      this.ReconnectIfWanted();
      return;
    }

    // The firmware serves /ws from the same HTTP server as the page (port 80).
    this.#socket = new WebSocket(`ws://${hostname}/ws`, 'flatbuffers');
    this.#socket.binaryType = 'arraybuffer';
    this.#socket.onopen = this.handleOpen.bind(this);
    this.#socket.onclose = this.handleClose.bind(this);
    this.#socket.onerror = this.handleError.bind(this);
    this.#socket.onmessage = this.handleMessage.bind(this);
  }
  public Disconnect() {
    this.#autoReconnect = false;
    this.clearTimers();

    const connectionState = this.ConnectionState;
    if (
      connectionState === ConnectionState.DISCONNECTED ||
      connectionState === ConnectionState.DISCONNECTING
    ) {
      return;
    }
    this.ConnectionState = ConnectionState.DISCONNECTING;

    if (this.#socket) {
      try {
        this.#socket.close();
        this.#abortTimer = setTimeout(() => {
          this.#abortTimer = null;
          this.AbortWebSocket();
        }, 1000);
      } catch {
        console.warn('[WS] Failed to gracefully close WebSocket connection, forcing close');
        this.AbortWebSocket();
      }
    }
  }
  private clearTimers() {
    if (this.#reconnectTimer !== null) {
      clearTimeout(this.#reconnectTimer);
      this.#reconnectTimer = null;
    }
    if (this.#abortTimer !== null) {
      clearTimeout(this.#abortTimer);
      this.#abortTimer = null;
    }
  }
  private ReconnectIfWanted() {
    this.AbortWebSocket();
    if (!this.#autoReconnect || this.#reconnectTimer !== null) {
      return;
    }

    const delay = this.#reconnectDelayMs;
    this.#reconnectDelayMs = Math.min(this.#reconnectDelayMs * 2, RECONNECT_DELAY_MAX_MS);
    this.#reconnectTimer = setTimeout(() => {
      this.#reconnectTimer = null;
      this.Connect();
    }, delay);
  }

  public Send(data: string | Blob | BufferSource): boolean {
    if (!this.#socket || this.#socket.readyState !== WebSocket.OPEN) {
      return false;
    }

    this.#socket.send(data);
    return true;
  }

  private handleOpen() {
    if (!this.#socket) {
      console.error('[WS] ERROR: Socket not initialized');
      this.ReconnectIfWanted();
      return;
    }

    this.#reconnectDelayMs = RECONNECT_DELAY_MIN_MS;
    this.#outageReported = false;
    this.ConnectionState = ConnectionState.CONNECTED;
  }
  private handleClose(ev: CloseEvent) {
    if (!ev.wasClean) {
      console.error('[WS] ERROR: Connection closed unexpectedly');
      // Report each outage once, not every failed reconnect attempt.
      if (!this.#outageReported) {
        this.#outageReported = true;
        toast.error('Websocket connection closed unexpectedly');
      }
    } else {
      console.log('[WS] Received disconnect: ', ev.reason);
    }
    this.ReconnectIfWanted();
  }
  private handleError() {
    console.error('[WS] ERROR: Connection error');
    this.ReconnectIfWanted();
  }
  private handleMessage(msg: MessageEvent<string | ArrayBuffer | Blob>) {
    if (!msg.data) {
      console.warn('[WS] Received empty message');
      return;
    }

    if (isArrayBuffer(msg.data)) {
      WebSocketMessageBinaryHandler(this, msg.data);
      return;
    }

    if (isString(msg.data)) {
      console.warn('[WS] Text messages are not supported, received: ', msg.data);
      return;
    }

    console.warn('[WS] Received unknown message type: ', msg.data);
  }
  private AbortWebSocket() {
    if (this.#socket) {
      try {
        this.#socket.onclose = null;
        this.#socket.onerror = null;
        this.#socket.onmessage = null;
        this.#socket.onopen = null;
        this.#socket.close();
      } catch (e) {
        console.error(e);
      }
    }
    this.#socket = null;
    this.ConnectionState = ConnectionState.DISCONNECTED;
  }
}
