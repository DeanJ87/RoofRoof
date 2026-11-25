import { FunctionalComponent } from 'preact';
import { useId, useState } from 'preact/hooks';
import { Button, Note } from '../components/ui';
import type { RoofView } from '../lib/roofView';
import { moveBlockers, type BlockerContext } from '../lib/moveBlockers';
import { movePlan } from '../lib/movePlan';
import { MoveDialog } from '../components/MoveDialog';
import type { RoofCommand } from '../lib/api';
import { t } from '../i18n';

// The Roof card's Open, Close and Stop (spec 009; spec 027 FR-011). Open and Close stay usable
// while a check blocks them: a short line says why, and a press opens the move dialog. They
// are disabled only when there is nothing to do or no live data. Stop shows only while it has
// something to stop (a move, a waiting close, a move waiting for the park) and is never disabled.

export type Send = (command: RoofCommand, override?: string[]) => void;

// Spec 034: one short line per button, from the shared blockers (lib/moveBlockers.ts); every reason
// in full is in the move dialog a press opens. A button at its end already, or held by a fault,
// gets no line: the state pill and the fault say it, and the button's tooltip repeats it.
interface Line {
  text: string;
  tone: 'muted' | 'warn' | 'bad';
}

const SAID = [
  'roof.reason.alreadyOpen',
  'roof.reason.opening',
  'roof.reason.alreadyClosed',
  'roof.reason.closing',
  'roof.state.fault',
] as const;

const lineFor = (action: 'open' | 'close', view: RoofView, ctx: BlockerContext): Line | null => {
  const button = view[action];
  if (button.blocked) {
    // The first (most important) reason; the dialog's title counts them all.
    const first = moveBlockers(view.roof, action, ctx).find((b) => b.id !== 'fault');
    return first ? { text: first.short, tone: first.tone } : null;
  }
  if (!button.disabled || !button.reason || SAID.some((key) => t(key) === button.reason)) return null;
  return { text: button.reason, tone: 'muted' };
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

// Under the buttons: each button's line (one, unprefixed, when both say the same), the notes and the last answer.
const Why: FunctionalComponent<{
  view: RoofView;
  lines: { open: Line | null; close: Line | null };
  ids: { open: string; close: string };
}> = ({ view, lines, ids }) => {
  const { open, close } = lines;
  const same = Boolean(open && close && open.text === close.text);
  return (
    <>
      {open && (
        <Note id={ids.open} tone={open.tone}>
          {same ? open.text : t('roof.reason.openPrefix', { reason: open.text })}
        </Note>
      )}
      {close && !same && (
        <Note id={ids.close} tone={close.tone}>
          {t('roof.reason.closePrefix', { reason: close.text })}
        </Note>
      )}
      {view.notes.map((note) => (
        <Note key={note.id} tone={note.tone}>
          {note.text}
        </Note>
      ))}
      {view.result && <Note tone={view.result.tone}>{view.result.text}</Note>}
    </>
  );
};

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
  describedBy: { open?: string; close?: string };
  stop: () => void;
}> = ({ view, press, describedBy, stop }) => (
  <div class="btn-row">
    <Button
      onClick={() => press('open')}
      disabled={view.open.disabled}
      busy={view.open.busy}
      busyLabel={t('roof.sending')}
      describedBy={describedBy.open}
      title={describedBy.open ? undefined : view.open.reason || undefined}
    >
      {t('roof.open')}
    </Button>
    <Button
      onClick={() => press('close')}
      disabled={view.close.disabled}
      busy={view.close.busy}
      busyLabel={t('roof.sending')}
      describedBy={describedBy.close}
      title={describedBy.close ? undefined : view.close.reason || undefined}
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
  const lines = { open: lineFor('open', view, ctx), close: lineFor('close', view, ctx) };
  const ids = { open: `${base}-open`, close: `${base}-close` };
  const same = lines.open?.text === lines.close?.text;
  const describedBy = { open: lines.open ? ids.open : undefined, close: lines.close ? (same ? ids.open : ids.close) : undefined };
  const press = (command: 'open' | 'close') => {
    if (move.press(command)) return setAsking(null);
    return confirm ? setAsking(command) : send(command);
  };
  return (
    <div class="roof-controls" data-inventory="roof-buttons">
      <ButtonRow
        view={view}
        press={press}
        describedBy={describedBy}
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
      <Why view={view} lines={lines} ids={ids} />
      {move.dialog}
    </div>
  );
};
