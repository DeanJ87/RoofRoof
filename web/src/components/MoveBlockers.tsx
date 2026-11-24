import { FunctionalComponent } from 'preact';
import { route } from 'preact-router';
import { useState } from 'preact/hooks';
import type { Blocker } from '../lib/moveBlockers';
import { bodyOf, releaseHold, roofCommand } from '../lib/api';
import { deviceError } from '../i18n/deviceMessage';
import { Button, InfoTip, Note } from './ui';
import { showToast } from './toast';
import { t } from '../i18n';

// Spec 026: the one rendering of "why can't the roof move?" (lib/moveBlockers.ts
// decides what it says). Each blocker: what stops it, "?" for what that means,
// when it clears, and its one fix: a page, Clear fault, or Release hold (asked
// again, attended, before it is sent). Built from Note and InfoTip (DS-04, DS-22).

const send = async (request: () => Promise<Response>, done?: string) => {
  try {
    const response = await request();
    if (response.ok) {
      if (done) showToast({ message: done });
      return;
    }
    showToast({ message: deviceError(await bodyOf(response), t('roof.result.refused')), tone: 'bad' });
  } catch {
    showToast({ message: t('settings.couldNotReachTheDevice'), tone: 'bad' });
  }
};

const Row: FunctionalComponent<{ blocker: Blocker; onDone?: () => void }> = ({ blocker, onDone }) => {
  const [asking, setAsking] = useState(false);
  const fix = blocker.fix;
  const act = async () => {
    if (!fix) return;
    if (fix.href) return route(fix.href);
    if (fix.act === 'releaseHold') return setAsking(true);
    await send(() => roofCommand('clear-fault'));
    onDone?.();
  };
  const release = async () => {
    setAsking(false);
    await send(releaseHold, t('move.release.done'));
    onDone?.();
  };
  return (
    <div class="move-blocker" data-blocker={blocker.id}>
      <Note tone={blocker.tone} action={fix && !asking ? { label: fix.label, onClick: () => void act() } : undefined}>
        {blocker.title} <InfoTip text={blocker.meaning} />
      </Note>
      <Note>{blocker.clears}</Note>
      {/* Asked again only while the release is still offered (the device refuses it otherwise). */}
      {asking && fix?.act === 'releaseHold' && (
        <div class="inline-confirm" role="group" aria-label={t('move.release.confirm')}>
          <span>{t('move.release.confirm')}</span>
          <Button onClick={() => void release()}>{t('move.release.yes')}</Button>
          <Button variant="ghost" onClick={() => setAsking(false)}>
            {t('roof.cancel')}
          </Button>
        </div>
      )}
    </div>
  );
};

/**
 * The blockers in order, the checks a test box bypasses (never shown as passing, constitution VII),
 * and with `ready`, a line saying nothing stops a move when the list is empty.
 */
export const MoveBlockers: FunctionalComponent<{ blockers: Blocker[]; bypassed?: string[]; ready?: boolean; onDone?: () => void }> = ({
  blockers,
  bypassed = [],
  ready,
  onDone,
}) => (
  <div class="move-blockers" data-move-blockers={blockers.length}>
    {blockers.map((blocker) => (
      <Row key={blocker.id} blocker={blocker} onDone={onDone} />
    ))}
    {ready && blockers.length === 0 && <Note tone="ok">{t('move.ready')}</Note>}
    {bypassed.length > 0 && <Note tone="warn">{t('move.bypassed', { checks: bypassed.join(', ') })}</Note>}
  </div>
);
