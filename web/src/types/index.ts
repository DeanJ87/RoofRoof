// The device's documents as the web UI reads them, by area.
export * from './status';
export * from './config';
export * from './alerts';
export * from './inputs';

export interface WiFiNetwork {
  ssid: string;
  rssi: number;
  encryption: 'open' | 'secured';
}

// ASCOM Alpaca response envelope (https://ascom-standards.org/api/)
export interface AlpacaResponse<T> {
  Value: T;
  ClientTransactionID: number;
  ServerTransactionID: number;
  ErrorNumber: number;
  ErrorMessage: string;
}

export interface AlpacaConfiguredDevice {
  DeviceName: string;
  DeviceType: string;
  DeviceNumber: number;
  UniqueID: string;
}

export interface AlpacaDeviceStateItem {
  Name: string;
  Value: number | boolean | string;
}
