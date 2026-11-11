import { FunctionalComponent } from 'preact';
import { useEffect } from 'preact/hooks';
import { useWebSocket } from '../hooks/useWebSocket';
import { useRoofCommand } from '../hooks/useRoofCommand';
import { stopBarShown, resultNote } from '../lib/roofView';
import type { SystemStatus } from '../types';
import { Button, Note } from './ui';
import { t } from '../i18n';

// Stop on every page while the roof moves (constitution VII; spec 009 FR-014).
// Pinned to the bottom of the page, not in the header or above the dashboard's
// cards (DS-01). It follows the last status it saw, so a roof last seen moving
// keeps its Stop while the live data is stale. Shares the page's /ws/status
// socket.
const barLabel = (status: SystemStatus | null) => {
  const state = status?.roof?.state;
  if (state === 'opening') return t('roof.state.opening');
  if (state === 'closing') return t('roof.state.closing');
  return t('roof.closeWaiting');
};

const RoofStopBar: FunctionalComponent = () => {
  const { data: status } = useWebSocket<SystemStatus>('/ws/status');
  const shown = stopBarShown(status);
  const { result, send } = useRoofCommand(status?.roof?.state ?? null);
  // The save bar moves up while this one shows (index.css).
  useEffect(() => {
    document.body.classList.toggle('has-stop-bar', shown);
    return () => document.body.classList.remove('has-stop-bar');
  }, [shown]);
  if (!shown) return null;
  const note = resultNote(result, status?.roof ?? null, Date.now());
  return (
    <div class="stop-bar" role="region" aria-label={t('roof.stopBar')} data-inventory="stop-bar">
      <span class="stop-bar-state">{barLabel(status)}</span>
      {note && <Note tone={note.tone}>{note.text}</Note>}
      {/* Never disabled, never busy (FR-003). */}
      <Button variant="danger" onClick={() => send('stop')}>
        {t('roof.stopRoof')}
      </Button>
    </div>
  );
};

export default RoofStopBar;
