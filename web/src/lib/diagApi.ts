import type { DiagDocument, DiagMove, DiagMoves } from '../types/diag';
import { ROOF_COMMAND_HEADER } from './api';

// Advanced diagnostics (spec 017, contracts/diagnostics.md). Readings are
// open; clearing the log is a person's command with the command header.

/** GET /api/diag: the live document, or `{enabled: false}`; null when unreachable. */
export const fetchDiag = async (): Promise<DiagDocument | null> => {
  try {
    const response = await fetch('/api/diag');
    return response.ok ? ((await response.json()) as DiagDocument) : null;
  } catch {
    return null;
  }
};

/** GET /api/diag/moves: the summaries, newest first; null when off or unreachable. */
export const fetchMoves = async (): Promise<DiagMoves | null> => {
  try {
    const response = await fetch('/api/diag/moves');
    const body = response.ok ? await response.json() : null;
    return body?.enabled && Array.isArray(body.moves) ? (body as DiagMoves) : null;
  } catch {
    return null;
  }
};

/** Where one move's profile is: JSON, or a CSV download. */
export const moveUrl = (id: number, csv = false) => `/api/diag/moves/${id}${csv ? '?format=csv' : ''}`;

/** GET /api/diag/moves/{id}: null when the move isn't held any more. */
export const fetchMove = async (id: number): Promise<DiagMove | null> => {
  try {
    const response = await fetch(moveUrl(id));
    return response.ok ? ((await response.json()) as DiagMove) : null;
  } catch {
    return null;
  }
};

/** POST /api/diag/moves/clear: 409 while the roof moves. Rejects when unreachable. */
export const clearMoves = (): Promise<Response> =>
  fetch('/api/diag/moves/clear', { method: 'POST', headers: { [ROOF_COMMAND_HEADER]: '1' } });

/** A download's body (the demo saves it in the page; the device sends an attachment). */
export const fetchFile = (url: string): Promise<Blob> => fetch(url).then((response) => response.blob());
