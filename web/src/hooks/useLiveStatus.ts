import { useEffect, useRef, useState } from 'preact/hooks';
import { useWebSocket } from './useWebSocket';
import { fetchStatus } from '../lib/settingsApi';
import type { SystemStatus } from '../types';

// Spec 045: a part of the status for a component on every page (the banners).
// From the page's live socket (/ws/status, which every page already has open
// for the Stop bar) while it delivers; read with GET /api/status every
// `pollMs` only while it doesn't (connecting, refused for too many pages, a
// device that can't send a frame). Each banner polled the full status every
// 5-10 s besides the live frames: a reply of about 9 KB on the device each time.
export const useLiveStatus = <T>(pick: (status: SystemStatus) => T | undefined, pollMs: number, refreshEvent?: string): T | undefined => {
  const { data } = useWebSocket<SystemStatus>('/ws/status');
  const [read, setRead] = useState<T | undefined>(undefined);
  const pickRef = useRef(pick); // a plain field reader: the poll uses the latest one
  pickRef.current = pick;
  const live = data !== null;
  const fromLive = live ? pick(data) : undefined;
  useEffect(() => {
    if (live) setRead(fromLive); // kept for when the socket goes quiet
  }, [live, fromLive]);
  useEffect(() => {
    if (live) return undefined;
    let alive = true;
    // An error answer (null) keeps the last known state.
    const poll = () =>
      fetchStatus()
        .then((status) => alive && status && setRead(pickRef.current(status)))
        .catch(() => undefined);
    poll();
    const timer = setInterval(poll, pollMs);
    if (refreshEvent) window.addEventListener(refreshEvent, poll);
    return () => {
      alive = false;
      clearInterval(timer);
      if (refreshEvent) window.removeEventListener(refreshEvent, poll);
    };
  }, [live, pollMs, refreshEvent]);
  return live ? fromLive : read;
};
