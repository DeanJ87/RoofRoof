import type { JogDirection, TeachPlace, TeachStatus } from '../types';
import { ROOF_COMMAND_HEADER, ROOF_COMMAND_TIMEOUT_MS, bodyOf, getJson, timeoutSignal } from './api';
import { deviceText } from '../i18n/deviceMessage';

// Jog and teach the ends (spec 019). Every POST carries the X-RoofRoof header
// like every roof command; the release is the ordinary Stop.

export interface TeachAnswer {
  ok: boolean;
  reason: string;
  text: string;
  id?: number;
  limitMs?: number;
}

/** GET /api/roof/teach, or null when the device can't be read. */
export const fetchTeach = async (): Promise<TeachStatus | null> => {
  try {
    return await getJson<TeachStatus>('/api/roof/teach');
  } catch {
    return null;
  }
};

const send = async (path: string, body?: unknown, timeoutMs = ROOF_COMMAND_TIMEOUT_MS): Promise<TeachAnswer> => {
  try {
    const headers: Record<string, string> = { [ROOF_COMMAND_HEADER]: '1' };
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    const response = await fetch(path, {
      method: 'POST',
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: timeoutSignal(timeoutMs),
    });
    const answer = await bodyOf(response);
    const text = typeof answer.error === 'string' ? deviceText(answer.error) : '';
    const reason = typeof answer.reason === 'string' ? answer.reason : String(response.status);
    const id = typeof answer.id === 'number' ? answer.id : undefined;
    const limitMs = typeof answer.limitMs === 'number' ? answer.limitMs : undefined;
    return { ok: response.ok, reason, text, id, limitMs };
  } catch {
    return { ok: false, reason: 'unreachable', text: '' };
  }
};

/** Starts a jog; the answer's `id` names it for the keep-alives. */
/** Starts a jog; `ignoreMagnets` (spec 033): the person overrides the end magnets (it runs on time alone). */
export const startJog = (direction: JogDirection, ignoreMagnets = false) =>
  send('/api/roof/jog', ignoreMagnets ? { direction, attended: true, ignoreMagnets } : { direction, attended: true });
/** Renews jog `id`; a late renewal can't outlive the device's own deadline, so it waits no longer than it. */
export const holdJog = (direction: JogDirection, id: number, timeoutMs: number) =>
  send('/api/roof/jog', { direction, attended: true, id }, timeoutMs);
/** Lets go of jog `id`: stops that jog only (a person's Stop button stays the ordinary Stop). */
export const releaseJog = (direction: JogDirection, id: number) => send('/api/roof/jog', { direction, attended: true, id, release: true });
export const markTeach = (at: TeachPlace) => send('/api/roof/teach/mark', { at });
export const confirmTeach = (values: { hallHighEnd: 'open' | 'closed'; hallHighCounts: number; hallLowCounts: number }) =>
  send('/api/roof/teach/confirm', values);
export const discardTeach = () => send('/api/roof/teach/discard');
