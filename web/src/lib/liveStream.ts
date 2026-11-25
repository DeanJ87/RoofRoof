// Live updates (spec 028). The device sends both streams on one schedule
// (lib/ConnectionBudget: liveIntervalMs); the demo follows it too. A page
// shows only readings from its current connection.

export const LIVE_REST_MS = 2000;
export const LIVE_MOVING_MS = 500;

/** How long after the last live update the next one is due (the device's schedule). */
export const liveIntervalMs = (roofMoving: boolean) => (roofMoving ? LIVE_MOVING_MS : LIVE_REST_MS);

/** A socket that has sent nothing for this long is dead: it is closed and opened again. */
export const LIVE_QUIET_MS = 5000;

const FIRST_RETRY_MS = 1000;
const MAX_RETRY_MS = 30000;
const RETRY_JITTER_MS = 1000;

/** The wait before reconnect attempt `retry` (0 first): 1 s doubling to 30 s, plus up to 1 s of jitter. */
export const reconnectDelay = (retry: number, random = Math.random()) =>
  Math.min(MAX_RETRY_MS, FIRST_RETRY_MS * 2 ** retry) + random * RETRY_JITTER_MS;

/** True once a socket has been quiet for LIVE_QUIET_MS since its last message (or since it opened). */
export const isQuiet = (since: number, now: number) => now - since >= LIVE_QUIET_MS;
