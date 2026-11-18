import { useEffect, useState } from 'preact/hooks';
import type { TeachStatus } from '../types';
import { fetchTeach } from '../lib/teachApi';

// The teach's status (spec 019), read twice a second while the card is open.
export const TEACH_POLL_MS = 500;

export const useTeach = () => {
  const [status, setStatus] = useState<TeachStatus | null>(null);
  const [tick, setTick] = useState(0);
  useEffect(() => {
    let live = true;
    const read = () =>
      void fetchTeach().then((next) => {
        if (live && next) setStatus(next);
      });
    read();
    const timer = window.setInterval(read, TEACH_POLL_MS);
    return () => {
      live = false;
      window.clearInterval(timer);
    };
  }, [tick]);
  return { status, refresh: () => setTick((n) => n + 1) };
};
