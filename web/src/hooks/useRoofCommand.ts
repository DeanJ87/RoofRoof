import { useEffect, useRef, useState } from 'preact/hooks';
import { bodyOf, roofCommand, type RoofCommand } from '../lib/api';
import type { CommandResult } from '../lib/roofView';
import type { RoofState } from '../types';
import { deviceText } from '../i18n/deviceMessage';
import { announce } from '../lib/a11y';
import { t } from '../i18n';

// Sends roof commands (spec 009 FR-006, FR-007, FR-009) through roofCommand(),
// the one helper that adds the X-RoofRoof header, and keeps the last answer.
// Stop is sent on every press, even while another command is in flight.

const text = (body: Record<string, unknown>, key: 'error' | 'message') => (typeof body[key] === 'string' ? deviceText(body[key]) : '');

/** One answer as a command result. */
export const readAnswer = async (
  command: RoofCommand,
  response: Response,
  at: number,
  stateAt: RoofState | null,
): Promise<CommandResult> => {
  const body = await bodyOf(response);
  const base = { command, at, stateAt, reason: typeof body.reason === 'string' ? body.reason : '' };
  if (response.status === 202) return { ...base, outcome: 'pending', text: text(body, 'message') };
  if (response.ok) return { ...base, outcome: 'accepted', text: '' };
  const outcome = response.status === 409 ? 'refused' : 'error';
  const why = response.status === 401 ? t('roof.result.loginNeeded') : text(body, 'error') || t('roof.result.refused');
  // A Stop that wasn't done says so first (FR-007).
  return { ...base, outcome, text: command === 'stop' ? t('roof.result.stopNotDone', { reason: why }) : why };
};

// A timeout may still have reached the device: Open and Close say to watch the roof.
const unreachableText = (command: RoofCommand, timedOut: boolean) => {
  if (command === 'stop') return t('roof.result.stopNotConfirmed');
  return timedOut ? t('roof.result.noAnswer') : t('roof.result.unreachable');
};

export const unreachable = (command: RoofCommand, at: number, stateAt: RoofState | null, timedOut = false): CommandResult => ({
  command,
  outcome: 'unreachable',
  reason: '',
  text: unreachableText(command, timedOut),
  at,
  stateAt,
});

/** Sends a command and records its answer, with the roof's state as last seen when the answer arrives. */
export const sendRoofCommand = async (command: RoofCommand, state: () => RoofState | null, now = Date.now): Promise<CommandResult> => {
  try {
    const response = await roofCommand(command);
    return await readAnswer(command, response, now(), state());
  } catch (error) {
    const timedOut = error instanceof DOMException && (error.name === 'TimeoutError' || error.name === 'AbortError');
    return unreachable(command, now(), state(), timedOut);
  }
};

export const useRoofCommand = (state: RoofState | null) => {
  const [result, setResult] = useState<CommandResult | null>(null);
  const [inFlight, setInFlight] = useState<RoofCommand | null>(null);
  const mounted = useRef(true);
  const current = useRef(state);
  current.current = state;
  useEffect(
    () => () => {
      mounted.current = false;
    },
    [],
  );

  const send = async (command: RoofCommand) => {
    // Stop never waits for, or blocks on, anything (constitution VII).
    if (command !== 'stop') setInFlight(command);
    const answer = await sendRoofCommand(command, () => current.current);
    if (!mounted.current) return;
    setResult(answer);
    // The note under the buttons, read out once (A11Y: status messages).
    if (answer.outcome !== 'accepted' && answer.text) announce(answer.text);
    if (command !== 'stop') setInFlight(null);
  };

  return { result, inFlight, send };
};

/** When a message last arrived after the live connection was lost (R3): a restart is over. */
export const useReconnectedAt = (connected: boolean, lastMessageAt: number | null) => {
  const lostAt = useRef<number | null>(null);
  const [reconnectedAt, setReconnectedAt] = useState<number | null>(null);
  useEffect(() => {
    if (!connected) lostAt.current ??= Date.now();
  }, [connected]);
  useEffect(() => {
    // Only a message newer than the loss: the last one before it doesn't count.
    if (connected && lostAt.current !== null && lastMessageAt !== null && lastMessageAt > lostAt.current) {
      lostAt.current = null;
      setReconnectedAt(lastMessageAt);
    }
  }, [connected, lastMessageAt]);
  return reconnectedAt;
};
