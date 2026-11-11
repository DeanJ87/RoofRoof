import { FunctionalComponent } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import { useDialogFocus } from '../lib/a11y';
import { Button, Note } from '../components/ui';
import { demoDevice } from './device';
import RealNotifications from './panel/RealNotifications';
import RoofControls from './panel/RoofControls';
import { tour } from './tour/tourState';

// The demo's own controls: the device's clock speed, the simulated roof's
// injectors (spec 011), real notifications and the tour. Only in the demo build
// (mounted from main.tsx).

// The flat RoofRoof mark, for 17 to 39 px (brand/README.md), inlined at 18 px.
// Decorative: the button's name stays "Demo". Source: brand/src/logo/roofroof-mark-flat.svg.
const DemoMark: FunctionalComponent = () => (
  <svg class="demo-panel-mark" width="18" height="18" viewBox="0 0 24 24" aria-hidden="true" focusable="false">
    <defs>
      <clipPath id="rr-demo-mark-clip">
        <rect x="2.25" y="2.25" width="19.5" height="19.5" rx="2.75" />
      </clipPath>
    </defs>
    <g clip-path="url(#rr-demo-mark-clip)">
      <rect x="2.25" y="2.25" width="19.5" height="19.5" fill="#1d2f41" />
      <circle cx="4.9" cy="5" r=".7" fill="#8b9baa" />
      <circle cx="11.2" cy="5.2" r=".6" fill="#8b9baa" />
      <path d="M2.25 18.5H21.75V21.75H2.25Z" fill="#13212f" />
      <path d="M8.6 11.6L6.1 7.2" fill="none" stroke="#e7edf3" stroke-width="1.8" stroke-linecap="round" />
      <rect x="4" y="12.4" width="8.6" height="6.1" fill="#8b9baa" />
      <rect x="18.9" y="12.4" width="1.2" height="6.1" fill="#8b9baa" />
      <rect x="4" y="11.4" width="16.5" height="1" fill="#8b9baa" />
      <path d="M12.1 11.4V9.9L16.3 7.5L20.5 9.9V11.4Z" fill="#55c7f2" />
    </g>
  </svg>
);

// The demo device's clock isn't the real time (SQMeter spec 019): say so where the demo is named (SQMeter spec 026).
const ClockMoved: FunctionalComponent = () =>
  Math.abs(demoDevice.now.valueOf() - Date.now()) > 60_000 ? (
    <span class="demo-clock-moved" data-inventory="demo-marker">
      Clock moved
    </span>
  ) : null;

// The device's clock at 1x or 10x, to see time-based alerts sooner.
const ClockSpeed: FunctionalComponent = () => (
  <div class="card-group" aria-label="Clock">
    <h3 class="card-group-title">Clock</h3>
    <div class="btn-row">
      {[1, 10].map((multiplier) => (
        <Button
          key={multiplier}
          small
          variant={demoDevice.timeMultiplier === multiplier ? 'primary' : 'ghost'}
          ariaLabel={`Clock speed ${multiplier}x${demoDevice.timeMultiplier === multiplier ? ' (current)' : ''}`}
          onClick={() => demoDevice.setTimeMultiplier(multiplier)}
        >
          {multiplier}x
        </Button>
      ))}
    </div>
  </div>
);

const DemoPanel: FunctionalComponent = () => {
  const [open, setOpen] = useState(false);
  const [, setTick] = useState(0);
  // Non-modal: focus moves in on open, Escape closes and focus returns to the
  // Demo button (SQMeter spec 022 FR-012).
  const body = useRef<HTMLElement>(null);
  const toggle = useRef<HTMLButtonElement>(null);
  useDialogFocus(open, body, toggle);

  useEffect(() => demoDevice.onChange(() => setTick((n) => n + 1)), []);
  useEffect(() => {
    document.body.classList.add('has-demo-panel');
    return () => document.body.classList.remove('has-demo-panel');
  }, []);

  return (
    <div class={`demo-panel${open ? ' is-open' : ''}`} onKeyDown={(event) => event.key === 'Escape' && setOpen(false)}>
      {open && (
        <section class="demo-panel-body" aria-label="Demo controls" ref={body}>
          <div class="demo-panel-head">
            <h2 tabIndex={-1} data-autofocus>
              Demo
            </h2>
            <Button variant="link" onClick={() => setOpen(false)}>
              Close
            </Button>
          </div>
          <Note>The device's own code runs in your browser. Nothing is sent anywhere unless you turn on Real notifications.</Note>
          <ClockSpeed />
          <RoofControls />
          <RealNotifications />
          <div class="btn-row">
            <Button
              small
              onClick={() => {
                setOpen(false);
                tour.start();
              }}
            >
              Take the tour
            </Button>
            <Button small variant="danger" onClick={() => demoDevice.reset()}>
              Reset demo
            </Button>
          </div>
        </section>
      )}
      <button ref={toggle} type="button" class="demo-panel-toggle" aria-expanded={open} onClick={() => setOpen(!open)}>
        <DemoMark /> Demo
        <ClockMoved />
      </button>
    </div>
  );
};

export default DemoPanel;
