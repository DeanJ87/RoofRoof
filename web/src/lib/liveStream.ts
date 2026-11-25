// Live updates (spec 028). The device sends both streams on one schedule
// (lib/ConnectionBudget: liveIntervalMs); the demo follows it too. A page
// treats only readings from its current connection as live; spec 037 keeps the
// last one on screen, greyed, while it reconnects.

export const LIVE_REST_MS = 2000;
export const LIVE_MOVING_MS = 500;

/** How long after the last live update the next one is due (the device's schedule). */
export const liveIntervalMs = (roofMoving: boolean) => (roofMoving ? LIVE_MOVING_MS : LIVE_REST_MS);

/**
 * A socket that has sent nothing for this long is dead: it is closed and opened again (after the
 * back-off). Spec 037: five rest intervals, so a frame or two lost to a busy device or WiFi
 * doesn't cost a reconnect; the device sends a heartbeat when it can't spare a whole reading.
 */
export const LIVE_QUIET_MS = 10000;

/** Spec 037: the device's close code for a page past its live-update limit (ConnectionBudget::TOO_MANY_PAGES). */
export const TOO_MANY_PAGES = 4429;
/** A refused page tries again this often (and when it becomes visible), never in a tight loop. */
export const TOO_MANY_RETRY_MS = 60000;

/** The device's message when it couldn't spare a whole reading: the connection is alive, nothing new. */
export const isHeartbeat = (message: unknown) =>
  typeof message === 'object' && message !== null && (message as { heartbeat?: unknown }).heartbeat === true;

const FIRST_RETRY_MS = 1000;
const MAX_RETRY_MS = 30000;
const RETRY_JITTER_MS = 1000;

/** The wait before reconnect attempt `retry` (0 first): 1 s doubling to 30 s, plus up to 1 s of jitter. */
export const reconnectDelay = (retry: number, random = Math.random()) =>
  Math.min(MAX_RETRY_MS, FIRST_RETRY_MS * 2 ** retry) + random * RETRY_JITTER_MS;

/** True once a socket has been quiet for LIVE_QUIET_MS since its last message (or since it opened). */
export const isQuiet = (since: number, now: number) => now - since >= LIVE_QUIET_MS;
