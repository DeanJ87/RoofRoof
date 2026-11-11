// The device's HTTP API as the web UI uses it (coding standard STRUCT-04):
// components ask here, or a hook in hooks/, instead of calling fetch.
// Each helper is a thin, behaviour-preserving wrapper: callers keep their own
// error handling, so a network failure still rejects.

/** GET (or `init`) `path` and return the raw response. */
export const request = (path: string, init?: RequestInit): Promise<Response> => fetch(path, init);

/** The parsed JSON body when the device answers 2xx, otherwise null. Rejects when unreachable. */
export const getJson = async <T>(path: string, init?: RequestInit): Promise<T | null> => {
  const response = await fetch(path, init);
  return response.ok ? ((await response.json()) as T) : null;
};

/** POST with no body. */
export const post = (path: string): Promise<Response> => fetch(path, { method: 'POST' });

/** POST `body` as JSON. */
export const postJson = (path: string, body: unknown): Promise<Response> =>
  fetch(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });

/** A response body as JSON, or {} when it isn't JSON (error bodies, empty replies). */
export const bodyOf = (response: Response): Promise<Record<string, unknown>> => response.json().catch(() => ({}));

/** The header every roof command carries: a cross-site form can't set it, so another page can't move the roof. */
export const ROOF_COMMAND_HEADER = 'X-RoofRoof';

export type RoofCommand = 'open' | 'close' | 'stop' | 'clear-fault';

/** A roof command with no answer by then counts as unreachable: a hung request never hides that Stop wasn't confirmed. */
export const ROOF_COMMAND_TIMEOUT_MS = 5000;

// AbortSignal.timeout where the browser has it, else the same from a controller.
export const timeoutSignal = (ms: number): AbortSignal => {
  if (typeof AbortSignal.timeout === 'function') return AbortSignal.timeout(ms);
  const controller = new AbortController();
  setTimeout(() => controller.abort(), ms);
  return controller.signal;
};

/** POST a roof command (spec 003). 409 answers say why it was refused (`reason`, `error`). */
export const roofCommand = (command: RoofCommand): Promise<Response> =>
  fetch(`/api/roof/${command}`, {
    method: 'POST',
    headers: { [ROOF_COMMAND_HEADER]: '1' },
    signal: timeoutSignal(ROOF_COMMAND_TIMEOUT_MS),
  });
