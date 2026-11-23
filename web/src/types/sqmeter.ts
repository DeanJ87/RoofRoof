// POST /api/sqmeter/site (spec 020, contracts/sqmeter-site-api.md): what the
// copy can use from SQMeter's settings. Every part after `device` is null on an
// older SQMeter without it; a secret is only ever a "set" flag.
export interface SqmeterSite {
  address: string;
  device: { name: string };
  location: { set: boolean; latitude: number; longitude: number } | null;
  time: { timezone: string; server1: string; server2: string } | null;
  mqtt: { broker: string; port: number; username: string; topic: string; passwordSet: boolean } | null;
  ntfy: { server: string; topic: string; tokenSet: boolean } | null;
  pushover: { userKeySet: boolean; appTokenSet: boolean } | null;
}
