import type { Config, RoofInputs } from '../../types';

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

// A source still waiting for its first reply. After a change, only the
// sources the change started again count; an unchanged one may still wait.
const waiting = (doc: RoofInputs, part: TestPart, before: RoofInputs | null, changed: boolean): boolean => {
  if (part === 'nina') return doc.nina.state === 'waiting';
  const sources = [
    [doc.weather.http, before?.weather.http],
    [doc.weather.mqtt, before?.weather.mqtt],
  ] as const;
  return sources.some(([now, was]) => now.state === 'waiting' && (!changed || !was || now.generation !== was.generation));
};

// The reading is of the saved settings: each source the settings switch on is
// being read, and each they switch off isn't (a reading taken as the device
// starts, before its first evaluation, says every source is off).
const matchesSettings = (doc: RoofInputs, part: TestPart, config: Config): boolean => {
  if (part === 'nina') return (doc.nina.state === 'notConfigured') === !config.park?.nina.url;
  const http = Boolean(config.safety?.http.enabled);
  const mqtt = Boolean(config.safety?.mqtt.enabled);
  return doc.weather.http.enabled === http && doc.weather.mqtt.enabled === mqtt;
};

/**
 * `before`: the document read before the save (null if it couldn't be read); `changed`: the save
 * changed this part; `config`: the settings as saved.
 */
export const testAnswered = (doc: RoofInputs, part: TestPart, before: RoofInputs | null, changed: boolean, config: Config): boolean => {
  if (waiting(doc, part, before, changed) || !matchesSettings(doc, part, config)) return false;
  if (!changed) return true;
  if (!before) return false; // can't tell an old reading from a new one
  const was = generations(before, part);
  return generations(doc, part).some((generation, i) => generation !== was[i]);
};
