import { useEffect, useState } from 'preact/hooks';
import type { CalibrationStatus } from '../types';
import { fetchCalibration } from '../lib/calibrationApi';

// The calibration run's status (spec 016), read once a second while the page is open.
export const CALIBRATION_POLL_MS = 1000;

export const useCalibration = () => {
  const [status, setStatus] = useState<CalibrationStatus | null>(null);
  const [tick, setTick] = useState(0);
  useEffect(() => {
    let live = true;
    const read = () =>
      void fetchCalibration().then((next) => {
        if (live && next) setStatus(next);
      });
    read();
    const timer = window.setInterval(read, CALIBRATION_POLL_MS);
    return () => {
      live = false;
      window.clearInterval(timer);
    };
  }, [tick]);
  return { status, refresh: () => setTick((n) => n + 1) };
};
