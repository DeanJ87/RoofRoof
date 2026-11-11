import { useEffect, useRef, useState } from 'preact/hooks';
import type { Config, RoofInputs } from '../types';
import { fetchInputs } from '../lib/setupApi';
import { TEST_LIMIT_MS, TEST_POLL_MS, testAnswered, type TestPart } from '../components/wizard/inputsTest';

// Test on the wizard's weather and N.I.N.A. pages (spec 015 FR-009): read the
// inputs, save the page, then read again once a second until the answer for
// the saved settings is in, for at most 15 s. Nothing is sent to SQMeter or
// N.I.N.A. but what the device polls anyway.

export type TestState =
  | { phase: 'idle' }
  | { phase: 'running' }
  | { phase: 'done'; doc: RoofInputs }
  | { phase: 'timeout'; doc: RoofInputs | null }
  | { phase: 'failed'; message: string };

// `before`: the inputs read before the save that the Test answers (when the page saved it).
export type TestSave = () => Promise<
  { ok: true; changed: boolean; config: Config; before?: RoofInputs | null } | { ok: false; message: string }
>;

const pause = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

export const useInputsTest = (part: TestPart) => {
  const [state, setState] = useState<TestState>({ phase: 'idle' });
  const live = useRef(true);
  useEffect(
    () => () => {
      live.current = false;
    },
    [],
  );

  const follow = async (before: RoofInputs | null, changed: boolean, config: Config) => {
    const deadline = Date.now() + TEST_LIMIT_MS;
    let last: RoofInputs | null = null;
    while (live.current && Date.now() < deadline) {
      const doc = await fetchInputs();
      if (doc) last = doc;
      if (doc && testAnswered(doc, part, before, changed, config)) return setState({ phase: 'done', doc });
      await pause(TEST_POLL_MS);
    }
    if (live.current) setState({ phase: 'timeout', doc: last });
  };

  const run = async (save: TestSave) => {
    setState({ phase: 'running' });
    const before = await fetchInputs();
    const saved = await save();
    if (!live.current) return;
    if (!saved.ok) return setState({ phase: 'failed', message: saved.message });
    await follow(saved.before !== undefined ? saved.before : before, saved.changed, saved.config);
  };

  return { state, run };
};
