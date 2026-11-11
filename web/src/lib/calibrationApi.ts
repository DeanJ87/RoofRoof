import type { CalibrationRequest, CalibrationStatus } from '../types';
import { ROOF_COMMAND_HEADER, ROOF_COMMAND_TIMEOUT_MS, bodyOf, getJson, timeoutSignal } from './api';
import { deviceText } from '../i18n/deviceMessage';

// The calibration run's routes (spec 016). Start and abort carry the
// X-RoofRoof header like every roof command.

export interface CalibrationAnswer {
  ok: boolean;
  reason: string;
  text: string;
}

/** GET /api/roof/calibration, or null when the device can't be read. */
export const fetchCalibration = async (): Promise<CalibrationStatus | null> => {
  try {
    return await getJson<CalibrationStatus>('/api/roof/calibration');
  } catch {
    return null;
  }
};

const send = async (path: string, body?: unknown): Promise<CalibrationAnswer> => {
  try {
    const headers: Record<string, string> = { [ROOF_COMMAND_HEADER]: '1' };
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    const response = await fetch(path, {
      method: 'POST',
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: timeoutSignal(ROOF_COMMAND_TIMEOUT_MS),
    });
    const answer = await bodyOf(response);
    const text = typeof answer.error === 'string' ? deviceText(answer.error) : '';
    return { ok: response.ok, reason: typeof answer.reason === 'string' ? answer.reason : String(response.status), text };
  } catch {
    return { ok: false, reason: 'unreachable', text: '' };
  }
};

export const startCalibration = (request: CalibrationRequest) => send('/api/roof/calibration/start', request);
export const abortCalibration = () => send('/api/roof/calibration/abort');
