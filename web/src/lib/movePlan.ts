import type { RoofOverride, RoofStatus } from '../types';
import { moveBlockers, type Blocker, type BlockerContext } from './moveBlockers';

// Spec 027 (FR-007, FR-009, FR-011): what pressing a blocked Open or Close does. The device decides
// every move and repeats every check; the page only lists the reasons and offers what a person may
// override: daylight and SQMeter's own "unsafe" for an Open (as `openOverridable` says now), the
// park check for one Close. The park is never overridden for an Open: the device asks N.I.N.A. to
// park first and opens once it is parked.

export interface MovePlan {
  action: 'open' | 'close';
  reasons: Blocker[]; // every reason, in the blockers' words
  override: RoofOverride[] | null; // what "Open anyway" or "Close anyway" names; null: nothing may be overridden
  park: boolean; // the mount isn't confirmed parked
}

const OVERRIDE: Record<string, RoofOverride | undefined> = { weather: 'weatherNotSafe', daylight: 'daylight' };

/** The dialog for a press of Open or Close, or null when the command just goes (nothing, or only the park, blocks an Open). */
export const movePlan = (roof: RoofStatus, action: 'open' | 'close', ctx: BlockerContext): MovePlan | null => {
  const reasons = moveBlockers(roof, action, ctx).filter((b) => b.id !== 'noLiveData');
  const park = reasons.some((b) => b.id === 'park');
  const others = reasons.filter((b) => b.id !== 'park');
  if (action === 'close') {
    if (others.length > 0) return { action, reasons, override: null, park };
    return park ? { action, reasons, override: ['mountNotParked'], park } : null;
  }
  if (others.length === 0) return null;
  const allowed = roof.openOverridable ?? [];
  const overridable = others.every((b) => allowed.includes(OVERRIDE[b.id] as RoofOverride));
  return { action, reasons, override: overridable ? allowed.filter((name) => name !== 'mountNotParked') : null, park };
};
