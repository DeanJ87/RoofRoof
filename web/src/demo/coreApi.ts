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
  // Spec 027: open and close take the optional override body ("" for none).
  roofCommand(name: string, body: string): string;
  // Spec 016: start (with the body), abort, status.
  calibration(action: string, body: string): string;
  // Spec 019: jog, mark, confirm, discard (with their bodies), status.
  teach(action: string, body: string): string;
  // Spec 020: POST /api/sqmeter/site from the simulated SQMeter's settings (the JSON body or "").
  sqmeterSite(body: string): string;
  // Spec 023: GET /api/pins ("status"), POST /api/pins/validate (with the body), and the wiring check ("check", "checkStatus").
  pins(action: string, body: string): string;
  // Spec 020: POST /api/sqmeter/test, one read of the simulated SQMeter's /api/safety.
  sqmeterTest(body: string): string;
  // Spec 027: POST /api/nina/test, one read of the simulated mount.
  ninaTest(body: string): string;
  roofInject(json: string): string;
  roofPanel(): string;
  platformHold(action: string): string;
  // Advanced diagnostics (spec 017, tools/demo-core/emulated_device_diag.cpp).
  diagDocument(): string;
  diagMoves(): string;
  diagMove(id: number, csv: boolean): string; // "" when the move isn't held
  diagClear(): string;
  // Spec 029: GET /api/diag/crashes and /api/diag/log (plain text), by the firmware's writers.
  crashesDocument(): string;
  logDocument(): string;
}
