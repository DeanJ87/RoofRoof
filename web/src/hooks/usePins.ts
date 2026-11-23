import { useEffect, useRef, useState } from 'preact/hooks';
import type { EthernetStatus, NetworkMode, PinsConfig, PinsDocument, PinValidation, WiringCheckDocument } from '../types';
import { fetchPins, fetchWiringCheck, startWiringCheck, validatePins } from '../lib/pinsApi';
import { fetchConfig, fetchStatus } from '../lib/settingsApi';
import { t } from '../i18n';

// The board and its pins (spec 023): GET /api/pins, live validation of an
// edited set, and the wiring check run to its end.

/** GET /api/pins once (and again on `reload`). */
export const usePinsDocument = () => {
  const [doc, setDoc] = useState<PinsDocument | null>(null);
  const [loading, setLoading] = useState(true);
  const [tick, setTick] = useState(0);
  useEffect(() => {
    let live = true;
    void fetchPins().then((next) => {
      if (!live) return;
      if (next) setDoc(next);
      setLoading(false);
    });
    return () => {
      live = false;
    };
  }, [tick]);
  return { doc, loading, reload: () => setTick((n) => n + 1) };
};

export const VALIDATE_DELAY_MS = 400;

export interface PinCheck {
  validation: PinValidation | null;
  checking: boolean;
  problem: string | null; // the device refused the set or couldn't be reached
}

/** The device's verdict on `pins` as edited, asked `delayMs` after the last change. */
export const usePinValidation = (pins: PinsConfig | undefined, delayMs = VALIDATE_DELAY_MS): PinCheck => {
  const [state, setState] = useState<PinCheck>({ validation: null, checking: false, problem: null });
  const key = JSON.stringify(pins ?? null);
  useEffect(() => {
    if (!pins || pins.preset === 'unset') return setState({ validation: null, checking: false, problem: null });
    let live = true;
    setState((prior) => ({ ...prior, checking: true }));
    const timer = window.setTimeout(async () => {
      const answer = await validatePins(pins);
      if (!live) return;
      if (answer.kind === 'checked') setState({ validation: answer.validation, checking: false, problem: null });
      else setState({ validation: null, checking: false, problem: answer.kind === 'refused' ? answer.text : t('pins.cantCheck') });
    }, delayMs);
    return () => {
      live = false;
      window.clearTimeout(timer);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps -- `key` is the pins' value; a new object with the same pins asks nothing
  }, [key, delayMs]);
  return state;
};

export const WIRING_POLL_MS = 1000;

/** The wiring check: the last result, Start, and polling until it is done. */
export const useWiringCheck = (pollMs = WIRING_POLL_MS) => {
  const [check, setCheck] = useState<WiringCheckDocument | null>(null);
  const [starting, setStarting] = useState(false);
  const [problem, setProblem] = useState<string | null>(null);
  const timer = useRef<number | undefined>(undefined);
  const poll = async () => {
    const next = await fetchWiringCheck();
    if (next) setCheck(next);
    if (next?.state === 'running') timer.current = window.setTimeout(() => void poll(), pollMs);
  };
  useEffect(() => {
    void poll();
    return () => window.clearTimeout(timer.current);
    // eslint-disable-next-line react-hooks/exhaustive-deps -- read once on mount; poll() keeps itself going
  }, []);
  const start = async () => {
    setStarting(true);
    setProblem(null);
    const answer = await startWiringCheck();
    setStarting(false);
    if (!answer.ok) return setProblem(answer.text);
    setCheck((prior) => ({ state: 'running', checks: prior?.checks ?? [] }));
    window.clearTimeout(timer.current);
    await poll();
  };
  const running = starting || check?.state === 'running';
  return { check, running, problem, start };
};

/** On a board with Ethernet: its status and the network mode saved (for pages outside Settings). */
export const useNetworkMode = () => {
  const [state, setState] = useState<{ ethernet: EthernetStatus | null; mode: NetworkMode | null }>({ ethernet: null, mode: null });
  useEffect(() => {
    let live = true;
    void Promise.all([fetchStatus().catch(() => null), fetchConfig().catch(() => null)]).then(([status, config]) => {
      if (live) setState({ ethernet: status?.ethernet ?? null, mode: config?.network?.mode ?? null });
    });
    return () => {
      live = false;
    };
  }, []);
  return state;
};
