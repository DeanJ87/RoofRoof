import { FunctionalComponent, VNode, render } from 'preact';
import { useEffect, useId, useLayoutEffect, useRef, useState } from 'preact/hooks';
import type { MovePlan } from '../lib/movePlan';
import { MoveBlockers } from './MoveBlockers';
import RoofStopBar from './RoofStopBar';
import { Button, Modal, Note } from './ui';
import { t } from '../i18n';

// Spec 027 (FR-011): a press of a blocked Open or Close. The wizard's modal dialog (DS-11) with the
// Stop bar inside it, every reason in the blockers' words, and the override only when every reason
// may be overridden (danger: the person takes the risk). Cancel after a Close that was sent ends it.
// Opened from a card, so it renders at the body (a card's transform would hold a fixed dialog) and
// makes the page behind it inert, as Layout does for the wizard.

const BEHIND = '.skip-link, .app-header, #main';

// The dialog in its own root at the body, the page behind it inert while it is there.
const AtBody: FunctionalComponent<{ dialog: VNode }> = ({ dialog }) => {
  const [host] = useState(() => document.createElement('div'));
  // First the dialog (its Modal notes the button that opened it), then the page goes inert.
  useLayoutEffect(() => render(dialog, host));
  useLayoutEffect(() => {
    const behind = Array.from(document.querySelectorAll<HTMLElement>(BEHIND));
    behind.forEach((element) => element.setAttribute('inert', ''));
    document.body.appendChild(host);
    return () => {
      render(null, host);
      host.remove();
      behind.forEach((element) => element.removeAttribute('inert'));
    };
  }, [host]);
  return null;
};

const whyKey = (plan: MovePlan) => {
  if (!plan.override) return 'move.dialog.hardWhy';
  return plan.action === 'open' ? 'move.dialog.openWhy' : 'move.dialog.closeWhy';
};

const Dialog: FunctionalComponent<{
  plan: MovePlan;
  onAnyway: () => void;
  onWait: () => void;
  onCancel: () => void;
}> = ({ plan, onAnyway, onWait, onCancel }) => {
  const id = useId();
  const heading = useRef<HTMLHeadingElement>(null);
  useEffect(() => heading.current?.focus(), []);
  const open = plan.action === 'open';
  const action = t(open ? 'roof.open' : 'roof.close');
  const footer = (
    <>
      {plan.override && (
        <Button variant="danger" onClick={onAnyway}>
          {t(open ? 'move.dialog.openAnyway' : 'move.dialog.closeAnyway')}
        </Button>
      )}
      {plan.override && !open && (
        <Button variant="primary" onClick={onWait}>
          {t('move.dialog.wait')}
        </Button>
      )}
      <Button variant="ghost" onClick={onCancel}>
        {t('roof.cancel')}
      </Button>
    </>
  );
  return (
    <Modal labelledBy={id} onEscape={onCancel} stopBar={<RoofStopBar />} footer={footer} aside={<p>{t(whyKey(plan))}</p>}>
      <h2 id={id} ref={heading} tabIndex={-1} data-move-dialog={plan.action}>
        {t('move.dialog.title', { count: plan.reasons.length, action })}
      </h2>
      <MoveBlockers blockers={plan.reasons} />
      {plan.override && !open && <Note tone="bad">{t('move.dialog.parkRisk')}</Note>}
      {plan.override && open && plan.park && <Note tone="warn">{t('move.dialog.parkFirst')}</Note>}
    </Modal>
  );
};

export const MoveDialog: FunctionalComponent<Parameters<typeof Dialog>[0]> = (props) => <AtBody dialog={<Dialog {...props} />} />;
