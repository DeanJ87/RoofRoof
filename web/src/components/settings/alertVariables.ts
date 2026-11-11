import type { AlertEventKey } from '../../types';

// The {variables} alert wording can use. The device fills the same names;
// lib/AlertLogic/template-variables.json is the shared list, and
// __tests__/alertVariables.test.ts fails if this file drifts from it.

// Every event.
export const COMMON_VARS = ['event', 'device', 'time', 'date', 'level'];

// The extra values an event adds.
export const EVENT_VARS: Partial<Record<AlertEventKey, string[]>> = {
  client_lost: ['silent_for', 'last_checked', 'client_id'],
  client_back: ['last_checked', 'client_id'],
  client_disconnected: ['client_id'],
};
