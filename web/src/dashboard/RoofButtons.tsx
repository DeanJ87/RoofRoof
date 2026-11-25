import { ComponentChildren, FunctionalComponent } from 'preact';
import { useId, useState } from 'preact/hooks';
import { Button, Note } from '../components/ui';
import type { ButtonView, RoofView } from '../lib/roofView';
import { moveBlockers, type Blocker, type BlockerContext } from '../lib/moveBlockers';
import { movePlan } from '../lib/movePlan';
import { MoveBlockers } from '../components/MoveBlockers';
import { MoveDialog } from '../components/MoveDialog';
import type { RoofCommand } from '../lib/api';
import { t } from '../i18n';

// The Roof card's Open, Close and Stop (spec 009; spec 027 FR-011). Open and Close stay usable
// while a check blocks them: the blockers below say why, and a press opens the move dialog. They
// are disabled only when there is nothing to do or no live data. Stop shows only while it has
// something to stop (a move, a waiting close, a move waiting for the park) and is never disabled.

export type Send = (command: RoofCommand, override?: string[]) => void;

// Spec 026: why Open or Close can't be used, from the shared blockers (the fault has its own
// block below). A reason no blocker covers ("Already open", "Restarting") stays a one-line note.
interface WhyView {
  blockers: Blocker[]; // both buttons' together, each once
  covered: { open: boolean; close: boolean }; // the button's own reason is in the list
}

const blockersFor = (view: RoofView, ctx: BlockerContext): WhyView => {
  const roof = view.roof;
  // At that end already (or moving there), only "no live data" explains more than the button does.
  const own = (action: 'open' | 'close', button: ButtonView, here: string[]) =>
    button.disabled || button.blocked
      ? moveBlockers(roof, action, ctx).filter((b) => b.id !== 'fault' && (b.id === 'noLiveData' || !here.includes(roof?.state ?? '')))
      : [];
  const open = own('open', view.open, ['open', 'opening']);
  const close = own('close', view.close, ['closed', 'closing']);
  // Spec 026: a reason that holds only one button says which ("Close: Park check ..."), so a park
  // blocker is never read as stopping the other.
  const only = (blocker: Blocker, other: Blocker[], key: 'roof.reason.openPrefix' | 'roof.reason.closePrefix') =>
    other.some((b) => b.id === blocker.id) ? blocker : { ...blocker, title: t(key, { reason: blocker.title }) };
  const both = [
    ...open.map((b) => only(b, close, 'roof.reason.openPrefix')),
    ...close.map((b) => only(b, open, 'roof.reason.closePrefix')),
  ];
  const seen = new Set<string>();
  const blockers = both.filter((blocker) => !seen.has(blocker.id) && Boolean(seen.add(blocker.id)));
  return { blockers, covered: { open: open.length > 0, close: close.length > 0 } };
};

// A button's reason no blocker covers ("Already closed", "Restarting"), as its own note.
const reasonNotes = (open: ButtonView, close: ButtonView, ids: { open: string; close: string }, covered: WhyView['covered']) => {
  const notes: ComponentChildren[] = [];
  // Spec 026: a reason both buttons share (a fault, a restart) is said once, for both, unprefixed.
  const shared = Boolean(open.reason) && open.reason === close.reason && !covered.open && !covered.close;
  if (open.reason && !covered.open)
    notes.push(
      <Note key="open" id={ids.open}>
        {shared ? open.reason : t('roof.reason.openPrefix', { reason: open.reason })}
      </Note>,
    );
  // A reason both share shows once, on the open note, unless that note isn't shown.
  if (close.reason && !covered.close && (close.reason !== open.reason || covered.open))
    notes.push(
      <Note key="close" id={ids.close}>
        {t('roof.reason.closePrefix', { reason: close.reason })}
      </Note>,
    );
  return notes;
};

// Spec 014: with "Ask before Open and Close" on, Open and Close ask once
// inline (the existing inline confirm pattern); Stop never asks (constitution VII).
const Confirm: FunctionalComponent<{ asking: 'open' | 'close'; onYes: () => void; onNo: () => void }> = ({ asking, onYes, onNo }) => {
  const question = asking === 'open' ? t('roof.confirmOpen') : t('roof.confirmClose');
  return (
    <div class="inline-confirm" role="group" aria-label={question} data-inventory="roof-confirm">
      <span>{question}</span>
      <Button onClick={onYes}>{asking === 'open' ? t('roof.open') : t('roof.close')}</Button>
      <Button variant="ghost" onClick={onNo}>
        {t('roof.cancel')}
      </Button>
    </div>
  );
};

// Under the buttons: the shared blockers (spec 026), a reason no blocker covers, the notes and the last answer.
const Why: FunctionalComponent<{ view: RoofView; why: WhyView; ids: { open: string; close: string }; whyId: string }> = ({
  view,
  why,
  ids,
  whyId,
}) => (
  <>
    {why.blockers.length > 0 && (
      <div id={whyId}>
        <MoveBlockers blockers={why.blockers} />
      </div>
    )}
    {reasonNotes(view.open, view.close, ids, why.covered)}
    {view.notes.map((note) => (
      <Note key={note.id} tone={note.tone}>
        {note.text}
      </Note>
    ))}
    {view.result && <Note tone={view.result.tone}>{view.result.text}</Note>}
  </>
);

// Spec 027: the move dialog's state. A Close blocked only by the park is sent at once (the device asks
// N.I.N.A. to park and waits when it can) unless the person asked to confirm first; Cancel then stops it.
const useMoveDialog = (view: RoofView, send: Send, confirm: boolean, ctx: BlockerContext) => {
  const [shown, setShown] = useState<{ action: 'open' | 'close'; sent: boolean } | null>(null);
  // Live: the reasons follow the roof, and the dialog goes once nothing blocks the move.
  const plan = shown && view.roof ? movePlan(view.roof, shown.action, ctx) : null;
  const press = (action: 'open' | 'close'): boolean => {
    const next = view[action].blocked && view.roof ? movePlan(view.roof, action, ctx) : null;
    if (!next) return false;
    const sent = action === 'close' && next.override !== null && !confirm;
    if (sent) send('close');
    setShown({ action, sent });
    return true;
  };
  const done = (command?: RoofCommand, override?: string[]) => {
    setShown(null);
    if (command) send(command, override);
  };
  const dialog = plan && shown && (
    <MoveDialog
      plan={plan}
      onAnyway={() => done(plan.action, plan.override ?? undefined)}
      onWait={() => done(shown.sent ? undefined : 'close')}
      onCancel={() => done(shown.sent ? 'stop' : undefined)}
    />
  );
  return { press, dialog };
};

// Open, Close, and Stop while it has something to stop.
const ButtonRow: FunctionalComponent<{
  view: RoofView;
  press: (command: 'open' | 'close') => void;
  describedBy: { open: string; close: string };
  stop: () => void;
}> = ({ view, press, describedBy, stop }) => (
  <div class="btn-row">
    <Button
      onClick={() => press('open')}
      disabled={view.open.disabled}
      busy={view.open.busy}
      busyLabel={t('roof.sending')}
      describedBy={view.open.reason ? describedBy.open : undefined}
    >
      {t('roof.open')}
    </Button>
    <Button
      onClick={() => press('close')}
      disabled={view.close.disabled}
      busy={view.close.busy}
      busyLabel={t('roof.sending')}
      describedBy={view.close.reason ? describedBy.close : undefined}
    >
      {t('roof.close')}
    </Button>
    {/* Never disabled, never busy (constitution VII, FR-003); spec 027: only while it has something to stop. */}
    {view.stopShown && (
      <Button variant="danger" onClick={stop}>
        {t('roof.stop')}
      </Button>
    )}
  </div>
);

export const RoofButtons: FunctionalComponent<{ view: RoofView; send: Send; confirm: boolean; ctx: BlockerContext }> = ({
  view,
  send,
  confirm,
  ctx,
}) => {
  const base = useId();
  const [asking, setAsking] = useState<'open' | 'close' | null>(null);
  const move = useMoveDialog(view, send, confirm, ctx);
  const why = blockersFor(view, ctx);
  const ids = { open: why.covered.open ? `${base}-why` : `${base}-open`, close: why.covered.close ? `${base}-why` : `${base}-close` };
  const sharedNote = !why.covered.open && !why.covered.close && view.close.reason && view.close.reason === view.open.reason;
  const closeId = sharedNote ? ids.open : ids.close;
  const press = (command: 'open' | 'close') => {
    if (move.press(command)) return setAsking(null);
    return confirm ? setAsking(command) : send(command);
  };
  return (
    <div class="roof-controls" data-inventory="roof-buttons">
      <ButtonRow
        view={view}
        press={press}
        describedBy={{ open: ids.open, close: closeId }}
        stop={() => {
          setAsking(null);
          send('stop');
        }}
      />
      {/* The question goes with the button: gone once the button is unavailable. */}
      {asking && !view[asking].disabled && (
        <Confirm
          asking={asking}
          onYes={() => {
            setAsking(null);
            send(asking);
          }}
          onNo={() => setAsking(null)}
        />
      )}
      <Why view={view} why={why} ids={ids} whyId={`${base}-why`} />
      {move.dialog}
    </div>
  );
};
