import type { SecretsProblem } from './status';

// Spec 024: a secret SQMeter shared, against RoofRoof's saved one: `received` (held by the device,
// differs), `same` (equal), `none` (not received, or empty). Never the value.
export type SecretState = 'none' | 'received' | 'same';
export interface SqmeterSecrets {
  problem: SecretsProblem | null;
  http: boolean; // they crossed the network unencrypted
  mqttPassword: SecretState;
  ntfyToken: SecretState;
  pushover: SecretState; // both keys
}

// POST /api/sqmeter/site (spec 020, contracts/sqmeter-site-api.md): what the
// copy can use from SQMeter's settings. Every part after `device` is null on an
// older SQMeter without it; a secret is only ever a "set" flag. Spec 024 adds the
// on/off switches and what became of SQMeter's secrets (absent on older firmware).
export interface SqmeterSite {
  address: string;
  device: { name: string };
  location: { set: boolean; latitude: number; longitude: number } | null;
  time: { timezone: string; server1: string; server2: string } | null;
  mqtt: { broker: string; port: number; username: string; topic: string; passwordSet: boolean; enabled?: boolean } | null;
  ntfy: { server: string; topic: string; tokenSet: boolean; enabled?: boolean } | null;
  pushover: { userKeySet: boolean; appTokenSet: boolean; enabled?: boolean } | null;
  secrets?: SqmeterSecrets;
}
