import { FunctionalComponent } from 'preact';
import { useEffect, useLayoutEffect, useRef, useState } from 'preact/hooks';
import { useWebSocket } from '../hooks/useWebSocket';
import { useRoofCommand } from '../hooks/useRoofCommand';
import { STOP_BAR_NO_STATUS_MS, resultNote, stopBarShown } from '../lib/roofView';
import type { SystemStatus } from '../types';
import { Button, Note } from './ui';
import { t } from '../i18n';

// Stop on every page while the roof moves (constitution VII; spec 009 FR-014).
// Pinned to the bottom of the page, not in the header or above the dashboard's
// cards (DS-01). It follows the last status it saw, so a roof last seen moving
// keeps its Stop while the live data is stale; with no status at all after a
// few seconds the roof's state is unknown, so Stop shows too. Shares the
// page's /ws/status socket.

const barLabel = (status: SystemStatus | null) => {
  const state = status?.roof?.state;
  if (!status) return t('roof.stopBarNoData');
  if (state === 'opening') return t('roof.state.opening');
  if (state === 'closing') return t('roof.state.closing');
  if (status.roof?.calibrating) return t('calibrate.stopBar');
  return t('roof.closeWaiting');
};

// True once `ms` have passed since the page opened.
const useAfter = (ms: number) => {
  const [passed, setPassed] = useState(false);
  useEffect(() => {
    const timer = window.setTimeout(() => setPassed(true), ms);
    return () => window.clearTimeout(timer);
  }, [ms]);
  return passed;
};

const RoofStopBar: FunctionalComponent = () => {
  const { data: status } = useWebSocket<SystemStatus>('/ws/status');
  const waited = useAfter(STOP_BAR_NO_STATUS_MS);
  const shown = stopBarShown(status, waited ? STOP_BAR_NO_STATUS_MS : 0);
  const { result, send } = useRoofCommand(status?.roof?.state ?? null);
  const bar = useRef<HTMLDivElement>(null);
  // The save bar and toasts move up by the bar's height while it shows, so
  // nothing covers Stop and Stop covers nothing (index.css).
  useLayoutEffect(() => {
    const body = document.body;
    body.classList.toggle('has-stop-bar', shown);
    const element = bar.current;
    const measure = () => element && body.style.setProperty('--stop-bar', `${Math.ceil(element.getBoundingClientRect().height)}px`);
    measure();
    // A rotated phone or a wrapping note changes the height without a re-render.
    const observer = element && typeof ResizeObserver === 'function' ? new ResizeObserver(measure) : null;
    if (element) observer?.observe(element);
    return () => {
      observer?.disconnect();
      body.classList.remove('has-stop-bar');
      body.style.removeProperty('--stop-bar');
    };
  });
  if (!shown) return null;
  const note = resultNote(result, status?.roof ?? null, Date.now());
  return (
    <div ref={bar} class="stop-bar" role="region" aria-label={t('roof.stopBar')} data-inventory="stop-bar">
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
