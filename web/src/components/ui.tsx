import { ComponentChildren, FunctionalComponent, type Ref } from 'preact';
import { useEffect, useId, useLayoutEffect, useRef, useState } from 'preact/hooks';
import { announce } from '../lib/a11y';
import { t } from '../i18n';

import { Icon, type IconTone } from './icons';

// The icon set lives in icons.tsx (kept apart for the file-length limit); it is part of the DS.
export { COLORS, Icon } from './icons';

export const Pill: FunctionalComponent<{ tone?: string }> = ({ tone = 'pill-dim', children }) => (
  <span class={`pill ${tone}`}>{children}</span>
);

export const Card: FunctionalComponent<{
  title: string;
  icon?: string;
  tone?: IconTone;
  actions?: ComponentChildren;
  hint?: ComponentChildren;
  id?: string;
}> = ({ title, icon, tone = 'cyan', actions, hint, id, children }) => (
  <section class="sq-card" id={id}>
    <div class="card-topline">
      <div class="card-title">
        {icon && <Icon name={icon} tone={tone} />}
        <h2>{title}</h2>
        {hint && <InfoTip text={hint} />}
      </div>
      {actions && <div class="card-actions">{actions}</div>}
    </div>
    {children}
  </section>
);

// `alt` is the same value in another unit, on its own line (DS-03: no wrapping "m/s (12 km/h)").
export const MetricTile: FunctionalComponent<{
  label: string;
  value: string;
  unit?: string;
  alt?: string;
  tone?: string;
  align?: 'left' | 'center';
}> = ({ label, value, unit, alt, tone = '', align = 'center' }) => (
  <div class={`metric-tile ${align === 'left' ? 'left' : ''}`}>
    <div class="metric-label">{label}</div>
    {/* A long value (e.g. 0.000289 lux) steps down a size rather than wrapping (DS-03). */}
    <div class={`metric-value ${tone}${value.length > 6 ? ' is-long' : ''}`}>{value}</div>
    {unit && <div class="metric-unit">{unit}</div>}
    {alt && <div class="metric-unit">{alt}</div>}
  </div>
);

export const ReadingRow: FunctionalComponent<{ label: string; value: string; labelClass?: string; valueClass?: string }> = ({
  label,
  value,
  labelClass = '',
  valueClass = '',
}) => (
  <div class="reading-row">
    <span class={`reading-label ${labelClass}`}>{label}</span>
    <strong class={`reading-value ${valueClass}`}>{value}</strong>
  </div>
);

export const SensorReadingRow: FunctionalComponent<{ label: string; value: string; unit: string }> = ({ label, value, unit }) => (
  <div class="reading-row sensor-reading">
    <span class="reading-label">{label}</span>
    <strong class="reading-value">
      {value} <em>{unit}</em>
    </strong>
  </div>
);

// `announceSteps`: for a running task (an update), announce each quarter
// reached - never every percent (SQMeter spec 022).
// `valueText`: what the value means ("Screen 3 of 20"), read instead of the percentage.
export const ProgressMeter: FunctionalComponent<{ value: number; label?: string; announceSteps?: boolean; valueText?: string }> = ({
  value,
  label = t('ui.progress'),
  announceSteps,
  valueText,
}) => {
  const percent = Math.round(Math.max(0, Math.min(100, value)));
  const quarter = Math.floor(percent / 25);
  const lastQuarter = useRef(quarter);
  useEffect(() => {
    if (announceSteps && quarter > lastQuarter.current) announce(`${label} ${quarter * 25}%`);
    lastQuarter.current = quarter;
  }, [quarter, announceSteps, label]);
  return (
    <div
      class="progress-meter"
      role="progressbar"
      aria-label={label}
      aria-valuemin={0}
      aria-valuemax={100}
      aria-valuenow={percent}
      aria-valuetext={valueText}
    >
      <div style={{ width: `${percent}%` }} />
    </div>
  );
};

type ButtonVariant = 'default' | 'primary' | 'danger' | 'ghost' | 'link';

// The only button style in the app. `default` is the tinted outline used for
// most actions, `primary` the one main action on a page (Save, Update),
// `danger` for destructive ones, `ghost` for quiet secondary actions and
// `link` for navigation.
export const Button: FunctionalComponent<{
  onClick?: () => void;
  variant?: ButtonVariant;
  small?: boolean;
  block?: boolean;
  disabled?: boolean;
  busy?: boolean;
  busyLabel?: string;
  title?: string;
  type?: 'button' | 'submit';
  ariaLabel?: string;
  describedBy?: string; // the id of the Note that says why it's disabled
}> = ({
  onClick,
  variant = 'default',
  small,
  block,
  disabled,
  busy,
  busyLabel,
  title,
  type = 'button',
  ariaLabel,
  describedBy,
  children,
}) => (
  <button
    type={type}
    title={title}
    aria-label={ariaLabel}
    aria-describedby={describedBy}
    class={`btn btn-${variant}${small ? ' btn-sm' : ''}${block ? ' btn-block' : ''}`}
    disabled={disabled || busy}
    onClick={onClick}
  >
    {busy ? (busyLabel ?? t('common.working')) : children}
  </button>
);

// "?" that reveals an explanation on hover, focus or tap - for detail that
// would otherwise clutter the page as footnotes. A button whose description
// is the tip, so screen readers read it; tap toggles it, Escape hides it
// (WCAG 1.4.13, 4.1.2).
export const InfoTip: FunctionalComponent<{ text: ComponentChildren }> = ({ text }) => {
  const id = useId();
  const [open, setOpen] = useState(false);
  const [dismissed, setDismissed] = useState(false);
  const reset = () => setDismissed(false);
  return (
    <button
      type="button"
      class={`info-tip${open ? ' is-open' : ''}${dismissed ? ' is-dismissed' : ''}`}
      aria-label={t('ui.moreInformation')}
      aria-describedby={id}
      aria-expanded={open}
      onClick={(event) => {
        event.preventDefault(); // inside a <label>: don't toggle its control
        setDismissed(false);
        setOpen(!open);
      }}
      onKeyDown={(event) => {
        // The first Escape hides the tip; once hidden, Escape reaches a dialog around it.
        if (event.key !== 'Escape' || dismissed) return;
        event.stopPropagation();
        setOpen(false);
        setDismissed(true);
      }}
      onBlur={() => {
        setOpen(false);
        reset();
      }}
      onMouseLeave={reset}
    >
      <span aria-hidden="true">?</span>
      <span class="info-tip-body" role="tooltip" id={id}>
        {text}
      </span>
    </button>
  );
};

// One-line status note under a control: no box, just tinted text.
export const Note: FunctionalComponent<{
  tone?: 'muted' | 'warn' | 'bad' | 'ok';
  action?: { label: string; onClick: () => void };
  id?: string;
}> = ({ tone = 'muted', action, id, children }) => (
  <p class={`note note-${tone}`} id={id}>
    {children}
    {action && (
      <button type="button" class="note-action" onClick={action.onClick}>
        {action.label}
      </button>
    )}
  </p>
);

/* ==== Onboarding (spec 018; constitution VII as amended in 2.1.0) =========
   The modal dialog, the choice cards, the speech bubble and the mascot, for a
   guided flow such as the setup wizard. Tokens only (index.css). */

const FOCUSABLE =
  'a[href],button:not([disabled]),input:not([disabled]):not([type="radio"]),input[type="radio"]:checked:not([disabled]),select:not([disabled]),textarea:not([disabled]),summary,[tabindex]:not([tabindex="-1"])';

// The tab stops inside `root`, in order. A radio group is one stop: its
// checked radio, or its first one while none is checked (as browsers do).
const tabStops = (root: HTMLElement): HTMLElement[] => {
  const stops = Array.from(root.querySelectorAll<HTMLElement>(FOCUSABLE));
  for (const group of Array.from(root.querySelectorAll('[role="radiogroup"]'))) {
    const first = group.querySelector<HTMLInputElement>('input[type="radio"]:not([disabled])');
    if (first && !group.querySelector('input:checked')) stops.push(first);
  }
  return stops.sort((a, b) => (a.compareDocumentPosition(b) & Node.DOCUMENT_POSITION_FOLLOWING ? -1 : 1));
};

/**
 * A modal dialog over the app (DS-11). Focus stays inside it (Tab and
 * Shift+Tab wrap), Escape calls `onEscape` (the caller decides: the wizard
 * asks before leaving), and focus goes back to where it was when the dialog
 * closes, or to the main region when that is gone. `stopBar` is required:
 * Stop must stay reachable while the roof moves (constitution VII), and
 * outside the dialog it would be hidden from screen readers and the keyboard.
 * The caller makes the rest of the app inert. The blur sits on a sibling of
 * the dialog, so the Stop bar inside stays fixed to the window.
 */
export const Modal: FunctionalComponent<{
  labelledBy: string;
  onEscape: () => void;
  stopBar: ComponentChildren;
  header?: ComponentChildren;
  footer?: ComponentChildren;
  aside?: ComponentChildren;
  class?: string;
}> = ({ labelledBy, onEscape, stopBar, header, footer, aside, class: className, children }) => {
  const root = useRef<HTMLDivElement>(null);
  // A layout effect: it runs before the caller moves focus into the dialog.
  useLayoutEffect(() => {
    const active = document.activeElement;
    const opener = active instanceof HTMLElement && !root.current?.contains(active) ? active : null;
    document.body.classList.add('has-modal');
    // Focus that lands outside (a click on the inert page) comes back in.
    const pull = (event: FocusEvent) => {
      const dialog = root.current;
      if (dialog && event.target instanceof Node && !dialog.contains(event.target)) tabStops(dialog)[0]?.focus();
    };
    document.addEventListener('focusin', pull);
    return () => {
      document.removeEventListener('focusin', pull);
      document.body.classList.remove('has-modal');
      // After the route that closed it has rendered.
      setTimeout(() => {
        const back = opener && opener !== document.body && opener.isConnected ? opener : document.getElementById('main');
        back?.focus();
      }, 0);
    };
  }, []);
  // Escape anywhere while the dialog is open (focus may sit on the page after a
  // disabled button lost it); a child that handles Escape stops it first.
  const escape = useRef(onEscape);
  escape.current = onEscape;
  useEffect(() => {
    const onEscapeKey = (event: KeyboardEvent) => {
      if (event.key !== 'Escape' || event.defaultPrevented) return;
      event.preventDefault();
      escape.current();
    };
    document.addEventListener('keydown', onEscapeKey);
    return () => document.removeEventListener('keydown', onEscapeKey);
  }, []);
  const onKeyDown = (event: KeyboardEvent) => {
    const dialog = root.current;
    if (event.key !== 'Tab' || !dialog) return;
    const stops = tabStops(dialog);
    if (stops.length === 0) return;
    const at = stops.indexOf(document.activeElement as HTMLElement);
    const wrap = event.shiftKey ? at <= 0 : at === stops.length - 1 || at === -1;
    if (!wrap) return;
    event.preventDefault();
    stops[event.shiftKey ? stops.length - 1 : 0].focus();
  };
  return (
    <div class="modal-root">
      <div class="modal-backdrop" aria-hidden="true" />
      <div
        ref={root}
        class={`modal${className ? ` ${className}` : ''}`}
        role="dialog"
        aria-modal="true"
        aria-labelledby={labelledBy}
        onKeyDown={onKeyDown}
      >
        <div class="modal-panel">
          {header && <div class="modal-header">{header}</div>}
          <div class="modal-body">
            <div class="modal-content">{children}</div>
            {aside && <aside class="modal-aside">{aside}</aside>}
          </div>
          {footer && <div class="modal-footer">{footer}</div>}
        </div>
        {stopBar}
        {/* Announcements while the dialog is open: a live region outside an aria-modal dialog may go unread. */}
        <div class="sr-only" role="status" aria-live="polite" data-modal-live />
      </div>
    </div>
  );
};

export interface Choice {
  value: string;
  title: string;
  detail?: string;
  icon?: string;
  badge?: string; // "Recommended"
}

/**
 * One question's answers as large cards (DS-12): a radio group of native
 * radios, so arrow keys move and wrap, Space selects and the group is one tab
 * stop. `value` null: nothing chosen yet. The checked card has an edge, a tint
 * and a check mark, never colour alone.
 */
export const ChoiceGroup: FunctionalComponent<{
  name: string;
  labelledBy: string;
  value: string | null;
  onChange: (value: string) => void;
  options: Choice[];
}> = ({ name, labelledBy, value, onChange, options }) => {
  const id = useId();
  return (
    <div class="choice-group" role="radiogroup" aria-labelledby={labelledBy} data-choice-group={name}>
      {options.map((option, index) => (
        <label key={option.value} class={`choice${option.value === value ? ' is-checked' : ''}`}>
          <input
            type="radio"
            class="choice-input"
            name={name}
            value={option.value}
            checked={option.value === value}
            aria-labelledby={`${id}-${index}`}
            aria-describedby={
              [option.badge && `${id}-${index}-b`, option.detail && `${id}-${index}-d`].filter(Boolean).join(' ') || undefined
            }
            onChange={() => onChange(option.value)}
          />
          {option.icon && (
            <span class="choice-icon">
              <Icon name={option.icon} size={20} />
            </span>
          )}
          <span class="choice-text">
            <span class="choice-title">
              <span id={`${id}-${index}`}>{option.title}</span>
              {option.badge && (
                <span id={`${id}-${index}-b`}>
                  <Pill tone="pill-cyan">{option.badge}</Pill>
                </span>
              )}
            </span>
            {option.detail && (
              <span class="choice-detail" id={`${id}-${index}-d`}>
                {option.detail}
              </span>
            )}
          </span>
          <span class="choice-mark" aria-hidden="true" />
        </label>
      ))}
    </div>
  );
};

/** RoofRoof's dog (DS-13): decoration only, in token colours. */
export const Mascot: FunctionalComponent<{ size?: number }> = ({ size = 56 }) => (
  <svg class="mascot" width={size} height={size} viewBox="0 0 64 64" aria-hidden="true">
    <path class="mascot-ear" d="M17 15c-7 1-11 11-9 21 1 6 7 6 9 1l5-16zM47 15c7 1 11 11 9 21-1 6-7 6-9 1l-5-16z" />
    <path class="mascot-fur" d="M32 12c11 0 18 8 18 19 0 12-8 19-18 19s-18-7-18-19c0-11 7-19 18-19z" />
    <ellipse class="mascot-ear" cx="24.5" cy="28" rx="5.5" ry="6" />
    <ellipse class="mascot-muzzle" cx="32" cy="39" rx="9.5" ry="7" />
    <circle class="mascot-eye" cx="24.5" cy="29" r="2.3" />
    <circle class="mascot-eye" cx="39.5" cy="29" r="2.3" />
    <path class="mascot-eye" d="M28.5 35h7c0 2.2-1.6 3.6-3.5 3.6s-3.5-1.4-3.5-3.6z" />
    <path class="mascot-line" d="M32 38.6v2.6m-4 1c1.6 1.3 3 1.3 4 0 1 1.3 2.4 1.3 4 0" />
    <path class="mascot-collar" d="M20 47c7 4.5 17 4.5 24 0" />
    <path class="mascot-tag" d="m32 50 1.3 2.7 3 .4-2.2 2 .6 3-2.7-1.5-2.7 1.5.6-3-2.2-2 3-.4z" />
  </svg>
);

/**
 * The mascot asking the screen's question (DS-13). The question is the
 * heading that names the dialog; focus moves to it on each new screen.
 */
export const SpeechBubble: FunctionalComponent<{ id: string; headingRef?: Ref<HTMLHeadingElement> }> = ({ id, headingRef, children }) => (
  <div class="speech">
    <Mascot />
    <div class="speech-bubble">
      <h2 id={id} ref={headingRef} tabIndex={-1}>
        {children}
      </h2>
    </div>
  </div>
);
