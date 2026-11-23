import type { AnalogConfig, PinsConfig, PinsDocument, PinValidation, WiringCheckDocument } from '../types';
import { bodyOf, getJson, post, postJson } from './api';
import { deviceError } from '../i18n/deviceMessage';
import { t } from '../i18n';

// The board, its pins and the wiring check (spec 023, contracts/pins-api.md).
// Saving the pins is the settings save's job (useConfigEditor), not a route here.

/** GET /api/pins: the pins in effect and saved, their problems and the presets; null when unreadable. */
export const fetchPins = async (): Promise<PinsDocument | null> => {
  try {
    return await getJson<PinsDocument>('/api/pins');
  } catch {
    return null;
  }
};

export type ValidateAnswer = { kind: 'checked'; validation: PinValidation } | { kind: 'refused'; text: string } | { kind: 'unreachable' };

/** POST /api/pins/validate: the device's rules on a pin set (and, with `analog`, the analog setup) it doesn't save. */
export const validatePins = async (pins: Partial<PinsConfig> & { analog?: AnalogConfig }): Promise<ValidateAnswer> => {
  try {
    const response = await postJson('/api/pins/validate', pins);
    const body = await bodyOf(response);
    if (!response.ok) return { kind: 'refused', text: deviceError(body, t('pins.cantCheck')) };
    return { kind: 'checked', validation: body as unknown as PinValidation };
  } catch {
    return { kind: 'unreachable' };
  }
};

/** POST /api/pins/check: starts the wiring check; a refusal carries the device's reason. */
export const startWiringCheck = async (): Promise<{ ok: boolean; text: string }> => {
  try {
    const response = await post('/api/pins/check');
    if (response.ok) return { ok: true, text: '' };
    return { ok: false, text: deviceError(await bodyOf(response), t('pins.check.refused')) };
  } catch {
    return { ok: false, text: t('pins.check.unreachable') };
  }
};

/** GET /api/pins/check: the last check's results; null when unreadable. */
export const fetchWiringCheck = async (): Promise<WiringCheckDocument | null> => {
  try {
    return await getJson<WiringCheckDocument>('/api/pins/check');
  } catch {
    return null;
  }
};
