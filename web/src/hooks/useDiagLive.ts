import type { DiagDocument, DiagLive } from '../types/diag';
import { useWebSocket } from './useWebSocket';

/**
 * The live diagnostics (spec 017) from /ws/diag. Spec 028: the document fetched
 * when the page opened stands in only until the stream first connects; after a
 * drop there is no reading (null) until the stream sends one.
 */
export const useDiagLive = (initial: DiagLive): DiagLive | null => {
  const { data, reconnecting } = useWebSocket<DiagDocument>('/ws/diag');
  if (data?.enabled) return data;
  return reconnecting ? null : initial;
};
