import { isArrayBuffer, isString } from '$lib/typeguards';
import { toast } from 'svelte-sonner';
import { WebSocketMessageBinaryHandler } from './MessageHandlers';
import { getDeviceHostname } from '$lib/utils/localRedirect';

export enum ConnectionState {
  DISCONNECTED = 0,
  DISCONNECTING,
  CONNECTING,
  CONNECTED,
}

export type ConnectionStateChangeHandler = (state: ConnectionState) => void;

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
  public Connect() {
    const connectionState = this.ConnectionState;
    if (
      connectionState === ConnectionState.CONNECTED ||
      connectionState === ConnectionState.CONNECTING
    ) {
      return;
    }

    this.AbortWebSocket();

    this.#autoReconnect = true;
    this.ConnectionState = ConnectionState.CONNECTING;

    const hostname = getDeviceHostname();
    if (!hostname) {
      console.error('[WS] ERROR: Failed to get WebSocket hostname');
      this.ReconnectIfWanted();
      return;
    }

    this.#socket = new WebSocket(`ws://${hostname}:81/ws`);
    this.#socket.binaryType = 'arraybuffer';
    this.#socket.onopen = this.handleOpen.bind(this);
    this.#socket.onclose = this.handleClose.bind(this);
    this.#socket.onerror = this.handleError.bind(this);
    this.#socket.onmessage = this.handleMessage.bind(this);
  }
  public Disconnect() {
    this.#autoReconnect = false;
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
        setTimeout(this.AbortWebSocket.bind(this), 1000);
      } catch {
        console.warn('[WS] Failed to gracefully close WebSocket connection, forcing close');
        this.AbortWebSocket();
      }
    }
  }
  private ReconnectIfWanted() {
    this.AbortWebSocket();
    if (this.#autoReconnect) {
      setTimeout(this.Connect.bind(this), 200);
    }
  }

  public Send(data: string | ArrayBufferLike | Blob | ArrayBufferView) {
    if (!this.#socket || this.#socket.readyState !== WebSocket.OPEN) {
      return;
    }

    this.#socket.send(data);
  }

  private handleOpen() {
    if (!this.#socket) {
      console.error('[WS] ERROR: Socket not initialized');
      this.ReconnectIfWanted();
      return;
    }
  }
  private handleClose(ev: CloseEvent) {
    if (!ev.wasClean) {
      console.error('[WS] ERROR: Connection closed unexpectedly');
      toast.error('Websocket connection closed unexpectedly');
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
        this.#socket.close();
        this.#socket.onclose = null;
        this.#socket.onerror = null;
        this.#socket.onmessage = null;
        this.#socket.onopen = null;
      } catch (e) {
        console.error(e);
      }
    }
    this.#socket = null;
    this.ConnectionState = ConnectionState.DISCONNECTED;
  }
}
