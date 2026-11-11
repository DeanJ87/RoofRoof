import type { RoofInputs } from '../../types';

// Whether a Test on the weather or N.I.N.A. page has its answer (spec 015
// FR-009, research R4). The device polls SQMeter and N.I.N.A. anyway; a saved
// change to a source starts its record again under a new generation. So for a
// part the save changed, the answer is the first reading of the new
// generation that isn't still waiting; for an unchanged part, the reading the
// device holds now.

export type TestPart = 'weather' | 'nina';

export const TEST_LIMIT_MS = 15000;
export const TEST_POLL_MS = 1000;

const generations = (doc: RoofInputs, part: TestPart): number[] =>
  part === 'weather' ? [doc.weather.http.generation, doc.weather.mqtt.generation] : [doc.nina.generation];

const waiting = (doc: RoofInputs, part: TestPart): boolean =>
  part === 'weather' ? [doc.weather.http, doc.weather.mqtt].some((source) => source.state === 'waiting') : doc.nina.state === 'waiting';

/** `before`: the document read before the save (null if it couldn't be read); `changed`: the save changed this part. */
export const testAnswered = (doc: RoofInputs, part: TestPart, before: RoofInputs | null, changed: boolean): boolean => {
  if (waiting(doc, part)) return false;
  if (!changed) return true;
  if (!before) return false; // can't tell an old reading from a new one
  const was = generations(before, part);
  return generations(doc, part).some((generation, i) => generation !== was[i]);
};
