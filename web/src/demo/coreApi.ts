import type { BackupCore } from './backup';

// What the device core (WebAssembly, tools/demo-core/bridge.cpp) exports.

export interface Core extends BackupCore {
  getConfig(redacted: boolean): string;
  applyConfig(json: string): string;
  loadConfig(json: string): boolean;
  restart(nowMs: number): void;
  tick(nowMs: number, epoch: number, localTime: string, localDate: string): void;
  statusParts(): string;
  // GET /api/roof/inputs (spec 015).
  inputsDocument(): string;
  effective(): string;
  recentAlerts(): string;
  clearAlerts(): void;
  isArmed(): boolean;
  armedDocument(): string;
  setArmed(on: boolean, source: string): void;
  testAlert(params: string): string;
  realTestAlert(): void;
  deliveryRequests(id: number, credentials: string): string;
  alpaca(method: string, path: string, params: string): string;
  saveState(): string;
  loadState(json: string): boolean;
  pending(): string;
  // The simulated roof (spec 011, tools/demo-core/emulated_device_roof.cpp).
  roofCommand(name: string): string;
  // Spec 016: start (with the body), abort, status.
  calibration(action: string, body: string): string;
  roofInject(json: string): string;
  roofPanel(): string;
  platformHold(action: string): string;
}
