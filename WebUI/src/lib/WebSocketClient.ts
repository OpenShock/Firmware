import { browser } from "$app/environment";
import { WiFiStateStore } from "./stores";
import type { WiFiNetwork } from "./types/WiFiNetwork";

export enum ConnectionState {
  DISCONNECTED = 0,
  DISCONNECTING,
  CONNECTING,
  CONNECTED,
}

export type ConnectionStateChangeHandler = (state: ConnectionState) => void;

export class WebSocketClient {
  public static readonly Instance = new WebSocketClient();

  private _socket: WebSocket | null = null;

  private _connectionState: ConnectionState = ConnectionState.DISCONNECTED;
  private _connectionStateChangeHandlers: ConnectionStateChangeHandler[] = [];
  private set ConnectionState(value: ConnectionState) {
    if (this._connectionState !== value) {
      this._connectionState = value;
      this._connectionStateChangeHandlers.forEach((handler) => handler(value));
    }
  }
  public get ConnectionState(): ConnectionState {
    return this._connectionState;
  }
  public addConnectionStateChangeHandler(handler: ConnectionStateChangeHandler) {
    this._connectionStateChangeHandlers.push(handler);
  }
  public removeConnectionStateChangeHandler(handler: ConnectionStateChangeHandler) {
    const index = this._connectionStateChangeHandlers.indexOf(handler);
    if (index !== -1) {
      this._connectionStateChangeHandlers.splice(index, 1);
    }
  }

  private _autoReconnect = false;
  public Connect() {
    const connectionState = this.ConnectionState;
    if (!browser || connectionState === ConnectionState.CONNECTED || connectionState === ConnectionState.CONNECTING) {
      return;
    }

    this.AbortWebSocket();

    this._autoReconnect = true;
    this.ConnectionState = ConnectionState.CONNECTING;

    this._socket = new WebSocket('ws://10.10.10.10:81/ws');
    this._socket.binaryType = 'arraybuffer';
    this._socket.onopen = this.handleOpen.bind(this);
    this._socket.onclose = this.handleClose.bind(this);
    this._socket.onerror = this.handleError.bind(this);
    this._socket.onmessage = this.handleMessage.bind(this);
  }
  public Disconnect() {
    this._autoReconnect = false;
    const connectionState = this.ConnectionState;
    if (connectionState === ConnectionState.DISCONNECTED || connectionState === ConnectionState.DISCONNECTING) {
      return;
    }
    this.ConnectionState = ConnectionState.DISCONNECTING;

    if (this._socket) {
      try {
        this._socket.close();
        setTimeout(this.AbortWebSocket.bind(this), 1000);
      } catch {
        console.warn('[WS] Failed to gracefully close WebSocket connection, forcing close');
        this.AbortWebSocket();
      }
    }
  }
  private ReconnectIfWanted() {
    this.AbortWebSocket();
    if (this._autoReconnect) {
      setTimeout(this.Connect.bind(this), 200);
    }
  }

  public Send(data: string | ArrayBufferLike | Blob | ArrayBufferView) {
    if (!this._socket || this._socket.readyState !== WebSocket.OPEN) {
      return;
    }

    this._socket.send(data);
  }

  private handleOpen() {
    if (!this._socket) {
      console.error('[WS] ERROR: Socket not initialized');
      this.ReconnectIfWanted();
      return;
    }
  }
  private handleClose(ev: CloseEvent) {
    if (!ev.wasClean) {
      console.error('[WS] ERROR: Connection closed unexpectedly');
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

    // Check if message is binary
    if (msg.data instanceof ArrayBuffer) {
      console.warn('[WS] Received binary message, not supported yet');
      return;
    }

    // Check if message is text
    if (typeof msg.data !== 'string') {
      console.log('[WS] Received message of unknown type');
      return;
    }

    // Parse message
    const message = JSON.parse(msg.data);
    if (!message) {
      console.warn('[WS] Received empty message');
      return;
    }

    if (message.networks !== undefined) {
      WiFiStateStore.setNetworks(message.networks as WiFiNetwork[]);
      return;
    }

    if (message.scanning !== undefined) {
      WiFiStateStore.setScanning(message.scanning as boolean);
      return;
    }
  }
  private AbortWebSocket() {
    if (this._socket) {
      try {
        this._socket.close();
        this._socket.onclose = null;
        this._socket.onerror = null;
        this._socket.onmessage = null;
        this._socket.onopen = null;
      } catch (e) {
        console.error(e);
      }
    }
    this._socket = null;
    this.ConnectionState = ConnectionState.DISCONNECTED;
  }
}
