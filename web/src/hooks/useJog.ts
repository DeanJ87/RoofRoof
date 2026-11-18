import { useEffect, useRef, useState } from 'preact/hooks';
import type { JogDirection } from '../types';
import { holdJog, releaseJog, startJog } from '../lib/teachApi';

// Hold-to-run (spec 019): press starts a jog, a keep-alive renews it every
// JOG_KEEPALIVE_MS while held, release stops that jog. The device stops the
// roof by itself 500 ms after the last keep-alive reached it, so a closed tab or
// a lost link stops it. A hold is released a second before the jog's limit
// (counted from the press), so a long hold ends as a stop, not a timeout fault.

export const JOG_KEEPALIVE_MS = 150;
export const JOG_HOLD_MS = 500;
export const JOG_RELEASE_BEFORE_LIMIT_MS = 1000;
const JOG_FALLBACK_LIMIT_MS = 10000; // a device that doesn't say: release early rather than late

interface Held {
  direction: JogDirection;
  pressedAt: number;
  id: number | null; // null until the start is answered
  released: boolean;
  renewing: boolean;
}

export const useJog = (onProblem: (text: string, reason: string) => void) => {
  const held = useRef<Held | null>(null);
  const timers = useRef<number[]>([]);
  const [active, setActive] = useState<JogDirection | null>(null);

  const end = () => {
    timers.current.forEach((timer) => window.clearInterval(timer));
    timers.current = [];
    held.current = null;
    setActive(null);
  };

  const release = () => {
    const now = held.current;
    if (!now || now.released) return;
    now.released = true;
    end();
    if (now.id !== null) void releaseJog(now.direction, now.id); // the device's own deadline stops it anyway
  };

  const renew = () => {
    const now = held.current;
    if (!now || now.id === null || now.renewing) return; // a slow answer: the next tick tries again
    now.renewing = true;
    void holdJog(now.direction, now.id, JOG_HOLD_MS).then((answer) => {
      now.renewing = false;
      if (answer.ok || held.current !== now || answer.reason === 'unreachable') return;
      end(); // the jog ended on the device (a magnet, a stop, a fault): nothing to renew
      onProblem(answer.text, answer.reason);
    });
  };

  const press = (direction: JogDirection) => {
    if (held.current) return;
    const now: Held = { direction, pressedAt: Date.now(), id: null, released: false, renewing: false };
    held.current = now;
    setActive(direction);
    void startJog(direction).then((answer) => {
      if (now.released) {
        if (answer.ok && answer.id !== undefined) void releaseJog(direction, answer.id); // released before the answer
        return;
      }
      if (!answer.ok || answer.id === undefined) {
        end();
        onProblem(answer.text, answer.reason);
        return;
      }
      now.id = answer.id;
      timers.current.push(window.setInterval(renew, JOG_KEEPALIVE_MS));
      const limitMs = answer.limitMs ?? JOG_FALLBACK_LIMIT_MS;
      const left = limitMs - JOG_RELEASE_BEFORE_LIMIT_MS - (Date.now() - now.pressedAt);
      timers.current.push(window.setTimeout(release, Math.max(0, left)));
    });
  };

  useReleaseOnLeave(release);
  return { active, press, release };
};

// The page hidden, the window losing focus or the card unmounting releases the hold.
const useReleaseOnLeave = (release: () => void) => {
  const latest = useRef(release);
  latest.current = release;
  useEffect(() => {
    const leave = () => latest.current();
    const hidden = () => {
      if (document.visibilityState === 'hidden') leave();
    };
    document.addEventListener('visibilitychange', hidden);
    window.addEventListener('blur', leave);
    return () => {
      document.removeEventListener('visibilitychange', hidden);
      window.removeEventListener('blur', leave);
      leave();
    };
  }, []);
};
