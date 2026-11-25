import { ComponentChildren, FunctionalComponent } from 'preact';
import { useId, useLayoutEffect, useRef, useState } from 'preact/hooks';
import { tipPlace } from '../lib/tipPlace';
import { t } from '../i18n';

// "?" that reveals an explanation on hover, focus or tap - for detail that
// would otherwise clutter the page as footnotes. A button whose description
// is the tip, so screen readers read it; tap toggles it, Escape hides it
// (WCAG 1.4.13, 4.1.2). Spec 028: the tip is shown in the top layer (popover),
// so no card, table or dialog covers or clips it; without it, it sits in place.
export const InfoTip: FunctionalComponent<{ text: ComponentChildren }> = ({ text }) => {
  const id = useId();
  const [open, setOpen] = useState(false); // tapped or clicked
  const [peek, setPeek] = useState(false); // a mouse over it, or keyboard focus; Escape ends it too
  const button = useRef<HTMLButtonElement>(null);
  const body = useRef<HTMLSpanElement>(null);
  const shown = open || peek;
  useLayoutEffect(() => {
    const tip = body.current!;
    const anchor = button.current!;
    if (!tip.showPopover) return;
    if (!shown) return tip.hidePopover();
    tip.showPopover();
    const place = () => {
      const spot = tipPlace(anchor.getBoundingClientRect(), tip.getBoundingClientRect(), { width: innerWidth, height: innerHeight });
      tip.style.top = `${spot.top}px`;
      tip.style.left = `${spot.left}px`;
    };
    place();
    addEventListener('scroll', place, true);
    return () => removeEventListener('scroll', place, true);
  }, [shown]);
  const hide = () => {
    setOpen(false);
    setPeek(false);
  };
  return (
    <button
      ref={button}
      type="button"
      class={`info-tip${shown ? ' is-shown' : ''}`}
      aria-label={t('ui.moreInformation')}
      aria-describedby={id}
      aria-expanded={open}
      onClick={(event) => {
        event.preventDefault(); // inside a <label>: don't toggle its control
        setPeek(false);
        setOpen(!open);
      }}
      onKeyDown={(event) => {
        // The first Escape hides the tip; once hidden, Escape reaches a dialog around it.
        if (event.key !== 'Escape' || !shown) return;
        event.stopPropagation();
        hide();
      }}
      onFocus={(event) => setPeek((event.currentTarget as HTMLElement).matches(':focus-visible'))}
      onBlur={hide}
      // A mouse only: a tap's emulated hover would keep the tip open after a second tap.
      onPointerEnter={(event) => setPeek(event.pointerType === 'mouse')}
      onPointerLeave={() => setPeek(false)}
    >
      <span aria-hidden="true">?</span>
      <span ref={body} class="info-tip-body" role="tooltip" id={id} popover="manual">
        {text}
      </span>
    </button>
  );
};
