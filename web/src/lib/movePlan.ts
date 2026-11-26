import type { RoofOverride, RoofStatus } from '../types';
import { moveBlockers, type Blocker, type BlockerContext } from './moveBlockers';

// Spec 027 (FR-007, FR-009, FR-011): what pressing a blocked Open or Close does. The device decides
// every move and repeats every check; the page only lists the reasons and offers what a person may
// override: daylight, SQMeter's own "unsafe" and (spec 039, constitution 10.0.0) the park for an Open
// (as `openOverridable` says now), the park for one Close. A move blocked only by the park is sent at
// once: the device asks N.I.N.A. to park and moves once it is parked, and the dialog offers to wait
// for that or to move anyway.

export interface MovePlan {
  action: 'open' | 'close';
  reasons: Blocker[]; // every reason, in the blockers' words
  override: RoofOverride[] | null; // what "Open anyway" or "Close anyway" names; null: nothing may be overridden
  park: boolean; // the mount isn't confirmed parked
  parkOnly: boolean; // the park is the only reason: the move may wait for it
}

const OVERRIDE: Record<string, RoofOverride | undefined> = { weather: 'weatherNotSafe', daylight: 'daylight', park: 'mountNotParked' };

/** The dialog for a press of Open or Close, or null when the command just goes. */
export const movePlan = (roof: RoofStatus, action: 'open' | 'close', ctx: BlockerContext): MovePlan | null => {
  const reasons = moveBlockers(roof, action, ctx).filter((b) => b.id !== 'noLiveData');
  if (reasons.length === 0) return null;
  const park = reasons.some((b) => b.id === 'park');
  const parkOnly = reasons.every((b) => b.id === 'park');
  if (action === 'close') return { action, reasons, override: parkOnly ? ['mountNotParked'] : null, park, parkOnly };
  const allowed = roof.openOverridable ?? [];
  const named = reasons.map((b) => OVERRIDE[b.id]);
  const overridable = named.every((name) => name !== undefined && allowed.includes(name));
  return { action, reasons, override: overridable ? allowed.filter((name) => named.includes(name)) : null, park, parkOnly };
};
