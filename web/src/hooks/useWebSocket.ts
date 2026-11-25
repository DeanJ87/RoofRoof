import { useEffect, useState } from 'preact/hooks';
import { announce } from '../lib/a11y';
import { TOO_MANY_PAGES, TOO_MANY_RETRY_MS, isHeartbeat, isQuiet, reconnectDelay } from '../lib/liveStream';
import { t } from '../i18n';

// One socket per URL, shared by every component that asks for it: the status
// socket is used by the page and the alerts bell at the same time, and the
// device has few connections to spare (16, see troubleshooting).
//
// Spec 028: a component only ever gets a reading from the current connection.
// When the socket closes, or sends nothing for LIVE_QUIET_MS (a phone that
// slept), the reading is dropped until the next one arrives, and the
// component learns whether it is connecting (nothing yet) or reconnecting.
// Spec 037: the last reading stays available as `lastKnown` (shown greyed,
// never as live); a heartbeat keeps the socket alive without a reading; the
// back-off only resets once a reading arrives, so a socket that opens and is
// closed again can't loop; a page refused for the device's limit says so and
// tries again only every minute or when it becomes visible.

export interface LiveSocket<T> {
  data: T | null; // the latest reading on the current connection, else null
  connected: boolean;
  lastMessageAt: number | null;
  reconnecting?: boolean; // true: a reading arrived before, none on this connection yet
  lastKnown?: T | null; // spec 037: the latest reading from any connection (not live once data is null)
  tooMany?: boolean; // spec 037: the device refused this page: too many pages are open
}

type View = LiveSocket<unknown>;
const EMPTY: View = { data: null, connected: false, lastMessageAt: null, reconnecting: false, lastKnown: null, tooMany: false };

type Shared = {
  url: string;
  socket: WebSocket | null;
  retry: number | null; // the back-off timer
  retryCount: number;
  // Connection loss and recovery are announced once each, not per retry
  // (SQMeter spec 022 FR-009).
  open: boolean;
  lost: boolean;
  since: number; // the last message, or when the socket was opened
  subscribers: Set<(view: View) => void>;
  view: View;
};

const sharedSockets = new Map<string, Shared>();

const update = (entry: Shared, change: Partial<View>) => {
  entry.view = { ...entry.view, ...change };
  entry.subscribers.forEach((set) => set(entry.view));
};

const clearRetry = (entry: Shared) => {
  if (entry.retry !== null) window.clearTimeout(entry.retry);
  entry.retry = null;
};

// The connection is gone: no reading from it is shown any more.
const dropConnection = (entry: Shared) => {
  entry.socket = null;
  if (entry.open) announce(t('webSocket.connectionToTheDeviceLost'));
  entry.lost ||= entry.open;
  entry.open = false;
  const { data, reconnecting } = entry.view;
  update(entry, { data: null, connected: false, lastMessageAt: null, reconnecting: data !== null || reconnecting });
};

const scheduleRetry = (entry: Shared, delay: number) => {
  clearRetry(entry);
  entry.retry = window.setTimeout(() => openSocket(entry), delay);
};

const openSocket = (entry: Shared) => {
  clearRetry(entry);
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const socket = new WebSocket(`${protocol}//${window.location.host}${entry.url}`);
  entry.socket = socket;
  entry.since = Date.now();

  socket.onopen = () => {
    entry.open = true;
    entry.since = Date.now();
    if (entry.lost) announce(t('webSocket.reconnectedToTheDevice'));
    entry.lost = false;
    update(entry, { connected: true });
  };

  socket.onmessage = (event) => {
    try {
      const message: unknown = JSON.parse(event.data);
      entry.since = Date.now();
      if (isHeartbeat(message)) return;
      entry.retryCount = 0;
      update(entry, { data: message, lastKnown: message, lastMessageAt: entry.since, reconnecting: false, tooMany: false });
    } catch {
      // A message that isn't JSON is ignored.
    }
  };

  socket.onclose = (event?: CloseEvent) => {
    if (entry.socket !== socket) return;
    dropConnection(entry);
    const tooMany = event?.code === TOO_MANY_PAGES;
    if (tooMany) update(entry, { tooMany });
    if (sharedSockets.get(entry.url) === entry) scheduleRetry(entry, tooMany ? TOO_MANY_RETRY_MS : reconnectDelay(entry.retryCount++));
  };
};

const closeSocket = (entry: Shared) => {
  const socket = entry.socket;
  if (!socket) return;
  socket.onclose = null;
  socket.close();
  dropConnection(entry);
};

// No socket, a closed one, or an open one that has gone quiet. One still
// connecting is left to the browser's own timeout (a slow link may need it).
const stale = (entry: Shared) => {
  const state = entry.socket?.readyState ?? WebSocket.CLOSED;
  return state === WebSocket.OPEN ? isQuiet(entry.since, Date.now()) : state !== WebSocket.CONNECTING;
};

// Now, not after the back-off.
const reconnectNow = (entry: Shared) => {
  closeSocket(entry);
  entry.retryCount = 0;
  openSocket(entry);
};

// A quiet socket on the ticker: closed, and opened again after the back-off.
const reconnectLater = (entry: Shared) => {
  closeSocket(entry);
  scheduleRetry(entry, reconnectDelay(entry.retryCount++));
};

// Every second: a socket that has gone quiet is dead (a phone that slept keeps
// it open). When the page comes back (visible again, out of the page cache, or
// the network is back), a closed or quiet socket reconnects at once.
let listening = false;
let ticker: number | null = null;
const watch = () => {
  ticker ??= window.setInterval(() => sharedSockets.forEach((entry) => entry.socket && stale(entry) && reconnectLater(entry)), 1000);
  if (listening) return;
  listening = true;
  const check = () => sharedSockets.forEach((entry) => stale(entry) && reconnectNow(entry));
  document.addEventListener('visibilitychange', () => document.visibilityState === 'visible' && check());
  window.addEventListener('pageshow', check);
  window.addEventListener('online', check);
};

const subscribe = (url: string, set: (view: View) => void) => {
  watch();
  let entry = sharedSockets.get(url);
  if (!entry) {
    entry = { url, socket: null, retry: null, retryCount: 0, open: false, lost: false, since: 0, subscribers: new Set(), view: EMPTY };
    sharedSockets.set(url, entry);
    openSocket(entry);
  }
  entry.subscribers.add(set);
  return entry;
};

// The last component left: closed quietly (nothing to announce, nobody to tell).
const unsubscribe = (url: string, set: (view: View) => void) => {
  const entry = sharedSockets.get(url);
  if (!entry?.subscribers.delete(set) || entry.subscribers.size > 0) return;
  sharedSockets.delete(url);
  clearRetry(entry);
  entry.open = false;
  closeSocket(entry);
  if (sharedSockets.size === 0 && ticker !== null) {
    window.clearInterval(ticker);
    ticker = null;
  }
};

export const useWebSocket = <T>(url: string): LiveSocket<T> => {
  const [view, setView] = useState<View>(() => sharedSockets.get(url)?.view ?? EMPTY);
  useEffect(() => {
    // A component joining an open socket starts from its latest reading.
    setView(subscribe(url, setView).view);
    return () => unsubscribe(url, setView);
  }, [url]);
  return view as LiveSocket<T>;
};
