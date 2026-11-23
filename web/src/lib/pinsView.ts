import type { BoardId, PinFunction, PinIssue, PinPreset, PinsConfig, PinsDocument, SystemStatus, WiringLevel } from '../types';
import { t, type MessageKey } from '../i18n';

// What the Pins page, Settings and the setup wizard show about the board and
// its pins (spec 023). Pure: the device decides what is allowed.

/** The functions in the order the wiring guide lists them. */
export const PIN_FUNCTIONS: PinFunction[] = ['rpwm', 'lpwm', 'enable', 'currentR', 'currentL', 'supply', 'hall', 'parkA', 'parkB'];

const PRESET_KEY: Record<PinPreset, MessageKey> = {
  devkit: 'pins.preset.devkit',
  'wt32-carrier-v1': 'pins.preset.wt32',
  custom: 'pins.preset.custom',
  unset: 'pins.preset.unset',
};
const BOARD_KEY: Record<BoardId, MessageKey> = { 'esp32-devkit': 'pins.boardName.devkit', 'wt32-eth01': 'pins.boardName.wt32' };

export const presetLabel = (preset: PinPreset): string => t(PRESET_KEY[preset]);
export const boardLabel = (board: BoardId): string => t(BOARD_KEY[board]);
export const functionLabel = (fn: PinFunction): string => t(`pins.fn.${fn}`);

/** A board with Ethernet: the WT32-ETH01. */
export const boardHasEthernet = (board: BoardId | undefined): boolean => board === 'wt32-eth01';

/** The board in effect has Ethernet (the status's Ethernet block, or the board the pins name). */
export const hasEthernet = (status: SystemStatus | null | undefined): boolean =>
  Boolean(status?.ethernet?.present || boardHasEthernet(status?.pins?.board));

/** The named preset's whole pin map, from the device's list. */
export const presetPins = (doc: PinsDocument | null, preset: 'devkit' | 'wt32-carrier-v1'): PinsConfig | null =>
  doc?.presets.find((map) => map.preset === preset) ?? null;

/** The preset to offer a device with no board chosen: the WT32's when its boot probe found one. */
export const suggestedPreset = (doc: PinsDocument | null): 'devkit' | 'wt32-carrier-v1' =>
  doc?.detected === 'wt32-eth01' ? 'wt32-carrier-v1' : 'devkit';

/** The pin set after choosing `preset`: a named one's map, or the current pins made custom. */
export const withPreset = (doc: PinsDocument | null, pins: PinsConfig, preset: PinPreset): PinsConfig => {
  if (preset === 'devkit' || preset === 'wt32-carrier-v1') return presetPins(doc, preset) ?? { ...pins, preset };
  return { ...pins, preset };
};

export const errorsOf = (issues: PinIssue[]) => issues.filter((issue) => issue.error);
export const warningsOf = (issues: PinIssue[]) => issues.filter((issue) => !issue.error);

const samePins = (a: PinsConfig | undefined, b: PinsConfig | undefined) => JSON.stringify(a) === JSON.stringify(b);

/** The saved pins aren't the ones in effect: a restart applies them. */
export const pinsRestartNeeded = (saved: PinsConfig | undefined, status: SystemStatus | null | undefined): boolean => {
  const running = status?.pins;
  if (!saved || !running) return false;
  return running.restartNeeded || running.preset !== saved.preset || running.board !== saved.board;
};

/** Why the wiring check can't start now, or null when it can. */
export const checkBlocked = (doc: PinsDocument | null): string | null => {
  if (!doc) return t('pins.check.blockedUnknown');
  if (doc.running.preset === 'unset') return t('pins.check.blockedUnset');
  if (doc.restartNeeded || !samePins(doc.running, doc.saved)) return t('pins.check.blockedRestart');
  if (!doc.valid) return t('pins.check.blockedInvalid');
  return null;
};

export const LEVEL_PILL: Record<WiringLevel, string> = { pass: 'pill-green', warn: 'pill-amber', fail: 'pill-red', skipped: 'pill-dim' };
