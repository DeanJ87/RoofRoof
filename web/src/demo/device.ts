import createRoofCore from './core/rr-core.mjs';
import type { BackupCore } from './backup';
import type { Core } from './coreApi';
import { formatIsoWithOffset, localClock } from './posixTz';

// The demo's emulated RoofRoof: the firmware's own logic (device core,
// WebAssembly), ticking once a second like the device. State lasts until the
// tab closes (sessionStorage). Spec 011 adds the simulated roof.

const STORAGE_KEY = 'roofroof.demo.v1';
// The latest release: what the demo device reports. Set at build time from
// CHANGELOG.md's newest released section (../lib/releaseVersion.ts).
export const DEMO_VERSION: string = import.meta.env.VITE_DEMO_VERSION ?? 'dev';
const LONDON_TZ = 'GMT0BST,M3.5.0/1,M10.5.0';

interface Saved {
  version: 1;
  device: string;
  timeMultiplier: number;
  demoMs: number;
  clockMs: number;
  savedAt: number;
}

// What the device is still waiting on.
export interface Pending {
  alerts: { condition: string; kind: 'grace' | 'settle' | 'cooldown'; remainingSeconds: number }[];
}

export interface Reply {
  status: number;
  body: string;
  contentType?: string;
}

type Listener = () => void;

class DemoDevice {
  private core!: Core;
  private demoMs = 1000; // device uptime clock (millis()), runs faster at 10x
  private clockMs = Date.now(); // device date and time (NTP), runs faster at 10x
  private lastWall = 0;
  private timer: ReturnType<typeof setInterval> | null = null;
  private listeners = new Set<Listener>();
  private restartingUntil = 0;
  timeMultiplier = 1;

  async start() {
    const module = await createRoofCore();
    this.core = new module.EmulatedDevice(JSON.stringify({ version: DEMO_VERSION, mac: 'a1b2c3d4e5f6' })) as Core;
    this.restore();
    this.lastWall = Date.now();
    this.step();
    this.timer = setInterval(() => this.step(), 1000);
  }

  stop() {
    if (this.timer) clearInterval(this.timer);
  }

  /** Device clock, ms since the emulated device was first started. */
  get nowMs() {
    return this.demoMs;
  }

  /** The device's date and time. */
  get now() {
    return new Date(this.clockMs);
  }

  /** The device's time zone (POSIX, from its settings). */
  get timezone(): string {
    return this.config().ntp?.timezone || 'UTC0';
  }

  /** "2026-10-08T23:10:00+0100": the device's local time, as /api/status reports it. */
  get isoTime() {
    return formatIsoWithOffset(this.timezone, this.clockMs);
  }

  get restarting() {
    return Date.now() < this.restartingUntil;
  }

  onChange(listener: Listener) {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private config() {
    return JSON.parse(this.core.getConfig(false));
  }

  private step() {
    const wall = Date.now();
    const elapsed = Math.max(0, wall - this.lastWall) * this.timeMultiplier;
    this.demoMs += elapsed;
    this.clockMs += elapsed;
    this.lastWall = wall;
    if (this.restarting) return;
    const { time, date } = localClock(this.timezone, this.clockMs);
    this.core.tick(this.demoMs, Math.floor(this.clockMs / 1000), time, date);
    this.persist();
    this.listeners.forEach((listener) => listener());
  }

  // --- Requests -------------------------------------------------------------

  effective = () => this.core.effective();
  recentAlerts = () => this.core.recentAlerts();
  /** What a real RoofRoof would send for alert `id` (SQMeter spec 018). */
  deliveryRequests = (id: number, credentials: string) => this.core.deliveryRequests(id, credentials);
  armedDocument = () => this.core.armedDocument();
  getConfig = () => this.core.getConfig(true);
  isArmed = () => this.core.isArmed();

  statusParts() {
    return JSON.parse(this.core.statusParts());
  }

  pending(): Pending {
    return JSON.parse(this.core.pending());
  }

  rawConfig = () => this.config();

  applyConfig(json: string): Reply {
    const result = this.core.applyConfig(json);
    const ok = !JSON.parse(result).error;
    if (ok) this.step();
    return { status: ok ? 200 : 400, body: result };
  }

  /** Pause or resume alerts; `source` is "ui", "rest" or "mqtt". */
  setArmed(on: boolean, source: string) {
    this.core.setArmed(on, source);
    this.step();
  }

  clearAlerts() {
    this.core.clearAlerts();
    this.persist();
  }

  testAlert(params: Record<string, string>): Reply {
    const reply = JSON.parse(this.core.testAlert(JSON.stringify(params)));
    this.persist();
    return { status: reply.status, body: JSON.stringify(reply.body) };
  }

  /** The Demo panel's "Send a test" for real notifications. */
  realTestAlert() {
    this.core.realTestAlert();
    this.persist();
  }

  alpaca(method: string, path: string, params: [string, string][]): Reply {
    const reply = JSON.parse(this.core.alpaca(method, path, JSON.stringify(params)));
    if (reply.status === 404)
      return { status: 400, body: 'Invalid Alpaca device type, device number, method or HTTP verb', contentType: 'text/plain' };
    const body = typeof reply.body === 'string' ? reply.body : JSON.stringify(reply.body);
    return { status: reply.status, body, contentType: reply.contentType };
  }

  /** The device core's backup routes (SQMeter spec 033), answered by demo/backup.ts. */
  backupCore = (): BackupCore => this.core;

  /** Restart the emulated device: a few seconds offline, then a new boot. */
  restart() {
    this.restartingUntil = Date.now() + 4000;
    setTimeout(() => {
      this.core.restart(this.demoMs);
      this.step();
    }, 4000);
  }

  setTimeMultiplier(multiplier: number) {
    this.timeMultiplier = multiplier;
    this.persist();
    this.listeners.forEach((listener) => listener());
  }

  reset() {
    try {
      sessionStorage.removeItem(STORAGE_KEY);
    } catch {
      // storage unavailable: nothing to clear
    }
    location.reload();
  }

  // --- Persistence --------------------------------------------------------------

  private persist() {
    try {
      const saved: Saved = {
        version: 1,
        device: this.core.saveState(),
        timeMultiplier: this.timeMultiplier,
        demoMs: this.demoMs,
        clockMs: this.clockMs,
        savedAt: Date.now(),
      };
      sessionStorage.setItem(STORAGE_KEY, JSON.stringify(saved));
    } catch {
      // Private mode or storage full: the demo keeps working without it.
    }
  }

  private restore() {
    let saved: Saved | null = null;
    try {
      saved = JSON.parse(sessionStorage.getItem(STORAGE_KEY) ?? 'null');
    } catch {
      saved = null;
    }
    if (saved?.version === 1 && this.core.loadState(saved.device)) {
      this.timeMultiplier = saved.timeMultiplier === 10 ? 10 : 1;
      const away = Math.max(0, Date.now() - saved.savedAt);
      this.demoMs = Math.max(1000, saved.demoMs + away);
      this.clockMs = saved.clockMs + away;
      return;
    }
    this.applyDefaults();
  }

  // A demo that looks like a working observatory from the first second.
  private applyDefaults() {
    this.core.applyConfig(
      JSON.stringify({
        deviceName: 'RoofRoof Demo',
        wifi: { ssid: 'DarkSkyLab', hostname: 'roofroof' },
        ntp: { timezone: LONDON_TZ },
        alpaca: { enabled: true },
        mqtt: { enabled: true, broker: '192.168.1.10', topic: 'roofroof' },
        alerts: { enabled: true, ntfy: { enabled: true, topic: 'roofroof-demo' } },
      }),
    );
    this.core.loadConfig(this.core.getConfig(false)); // what the hardware boots with
  }
}

export const demoDevice = new DemoDevice();
