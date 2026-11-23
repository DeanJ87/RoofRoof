import type { Config, SystemStatus } from '../types';
import { bodyOf, getJson, post, postJson, request } from './api';
import { deviceError } from '../i18n/deviceMessage';
import { t } from '../i18n';

// What Settings reads from and writes to the device (STRUCT-04).

/** The stored settings, as the device sends them (normalised by the caller). Rejects when unreachable or not JSON. */
export const fetchConfig = async (): Promise<Config> => (await request('/api/config')).json();

/** The header GET /api/config carries the settings revision in (spec 021). */
export const CONFIG_REVISION_HEADER = 'X-Config-Revision';

/** The stored settings and the revision they were read at (undefined from a device that doesn't send one). */
export const fetchConfigWithRevision = async (): Promise<{ config: Config; revision: number | undefined }> => {
  const response = await request('/api/config');
  const header = response.headers.get(CONFIG_REVISION_HEADER);
  const revision = header !== null && /^\d+$/.test(header) ? Number(header) : undefined;
  return { config: (await response.json()) as Config, revision };
};

/** The live status, or null when the device answers with an error. */
export const fetchStatus = () => getJson<SystemStatus>('/api/status');

export type RestartAnswer = { ok: true } | { ok: false; text: string };

/**
 * Ask the device to restart: `ok` once it accepted; otherwise why not (its own refusal, such as
 * the roof being busy, or that it couldn't be reached). Only an accepted restart is worth waiting for.
 */
export const restartDevice = async (): Promise<RestartAnswer> => {
  try {
    const response = await post('/api/restart');
    if (response.ok) return { ok: true };
    return { ok: false, text: deviceError(await bodyOf(response), t('settings.restartRefused')) };
  } catch {
    return { ok: false, text: t('settings.couldNotReachTheDevice') };
  }
};

/** The device's answer to a save; `conflicts` (spec 021) names what changed on the device since the page loaded it. */
export type SaveAnswer = { success?: boolean; error?: unknown; revision?: unknown; conflicts?: unknown };

/** Save the settings (a partial document): whether the device accepted them, and its JSON answer if it sent one. */
export const saveConfig = async (payload: object): Promise<{ ok: boolean; body: SaveAnswer | null }> => {
  const response = await postJson('/api/config', payload);
  const body = (response.headers.get('content-type') || '').includes('application/json') ? await response.json() : null;
  return { ok: response.ok, body };
};

// Spec 022: fired after a settings save, so the test box banner reads the status again at once.
export const TEST_BOX_CHANGED = 'roofroof:config-saved';
