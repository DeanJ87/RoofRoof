import type { RoofInputs, SetupConfig } from '../types';
import { getJson, postJson } from './api';
import { fetchConfig } from './settingsApi';

// The setup wizard's state and the readings its Tests show (spec 015, STRUCT-04).

/** The setup state, or null on firmware without it (or when the device can't be read). */
export const fetchSetup = async (): Promise<SetupConfig | null> => {
  try {
    return (await fetchConfig()).setup ?? null;
  } catch {
    return null;
  }
};

/** Save only the setup state (the device keeps every other setting). True when the device took it. */
export const saveSetup = async (setup: SetupConfig): Promise<boolean> => {
  try {
    const response = await postJson('/api/config', { setup });
    return response.ok;
  } catch {
    return false;
  }
};

/** GET /api/roof/inputs: the weather sources, the park and N.I.N.A. as last evaluated; null when unreadable. */
export const fetchInputs = async (): Promise<RoofInputs | null> => {
  try {
    return await getJson<RoofInputs>('/api/roof/inputs');
  } catch {
    return null;
  }
};
