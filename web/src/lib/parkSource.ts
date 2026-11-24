import type { Config, ParkConfig, ParkSource, ParkSwitches } from '../types';
import type { ConfigPath } from '../components/settings/context';

// Spec 024: what confirms the mount's park (N.I.N.A., the park switches, or
// both) and how many switches are fitted, shared by Settings → Roof and the
// setup wizard. The device enforces the rules; this keeps the page's edits
// within them so a save isn't refused.

/** The park source; settings from before spec 024 take it from the switches (none: N.I.N.A. only). */
export const parkSource = (park: ParkConfig): ParkSource => park.source ?? (park.switches === 'none' ? 'nina' : 'ninaAndSwitches');

/** N.I.N.A. counts for the park (with switches only its address is optional). */
export const ninaCounts = (park: ParkConfig): boolean => parkSource(park) !== 'switches';

export const fittedCount = (switches: ParkSwitches): number => ({ none: 0, one: 1, two: 2 })[switches];

/**
 * The edits for choosing `source`: N.I.N.A. only fits no switch; a switch source keeps the count, or
 * one when none was fitted. With `testBox` (the wizard), switches only clears the test box's park
 * bypass, which stands in for N.I.N.A. and can't be set then.
 */
export const parkSourceChanges = (park: ParkConfig, source: ParkSource, testBox?: Config['testBox']): [ConfigPath, unknown][] => {
  const changes: [ConfigPath, unknown][] = [[['park', 'source'], source]];
  if (source === 'nina') changes.push([['park', 'switches'], 'none']);
  else if (park.switches === 'none') changes.push([['park', 'switches'], 'one']);
  if (source === 'switches' && testBox?.park) changes.push([['testBox', 'park'], false]);
  return changes;
};

/**
 * Owner decision 2026-11-24: "No telescope under this roof" on clears the N.I.N.A. address in the same save
 * (the device does the same); off again before saving, the saved address comes back. Once saved, it stays gone.
 */
export const parkBypassChanges = (on: boolean, url: string, savedUrl: string): [ConfigPath, unknown][] => [
  [['testBox', 'park'], on],
  [['park', 'nina', 'url'], on ? '' : url || savedUrl],
];

/** The edits for the switch count: the source always goes with it (the device derives it otherwise). */
export const parkSwitchesChanges = (park: ParkConfig, switches: ParkSwitches): [ConfigPath, unknown][] => [
  [['park', 'source'], parkSource(park)],
  [['park', 'switches'], switches],
];

export type ParkWarning = 'oneSwitch' | 'wiring' | 'testBox';

/**
 * What to say about the switches: with switches only, one switch can't tell a short or a stuck switch
 * from a parked mount (two on separate cables can), each must close at park, and a switch that never
 * opens blocks Open; the test box's park bypass can't be set then.
 */
export const parkWarnings = (park: ParkConfig, testBox?: Config['testBox']): ParkWarning[] => {
  if (parkSource(park) !== 'switches') return [];
  const out: ParkWarning[] = [];
  if (park.switches === 'one') out.push('oneSwitch');
  out.push('wiring');
  if (testBox?.park) out.push('testBox');
  return out;
};
