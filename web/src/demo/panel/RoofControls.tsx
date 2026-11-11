import { FunctionalComponent } from 'preact';
import { Button } from '../../components/ui';
import { demoDevice, type RoofInjection, type RoofPanel } from '../device';

// Demo panel section (spec 011 FR-015): make things go wrong on purpose. Each
// control changes what the simulated SQMeter, N.I.N.A., roof or supply reports;
// what happens next is the device's own logic (verdict, close sequence,
// motion faults), as on a real RoofRoof.

type Weather = 'safe' | 'unsafe' | 'rain';

const WEATHER: { value: Weather; label: string }[] = [
  { value: 'safe', label: 'Safe' },
  { value: 'unsafe', label: 'Unsafe' },
  { value: 'rain', label: 'Rain' },
];

const weatherOf = (panel: RoofPanel): Weather => (panel.rain ? 'rain' : panel.unsafe ? 'unsafe' : 'safe');

const inject = (change: RoofInjection) => demoDevice.roofInject(change);

// One choice from a few, as pressed buttons (the Clock section's pattern).
const Choice: FunctionalComponent<{
  title: string;
  options: { value: string; label: string }[];
  current: string;
  onPick: (value: string) => void;
}> = ({ title, options, current, onPick }) => (
  <div class="demo-roof-row" role="group" aria-label={title}>
    <span class="demo-roof-label">{title}</span>
    <div class="btn-row">
      {options.map((option) => (
        <Button
          key={option.value}
          small
          variant={current === option.value ? 'primary' : 'ghost'}
          ariaLabel={`${title}: ${option.label}${current === option.value ? ' (current)' : ''}`}
          onClick={() => onPick(option.value)}
        >
          {option.label}
        </Button>
      ))}
    </div>
  </div>
);

const OFF_ON = [
  { value: 'off', label: 'Off' },
  { value: 'on', label: 'On' },
];

const MOUNT_TEXT: Record<RoofPanel['mount'], string> = {
  parked: 'Parked',
  unparked: 'Unparked',
  slewing: 'Slewing to park',
};

const RoofControls: FunctionalComponent = () => {
  const panel = demoDevice.roofPanel();
  return (
    <div class="card-group demo-roof" aria-label="Roof">
      <h3 class="card-group-title">Roof</h3>
      <Choice
        title="Weather"
        options={WEATHER}
        current={weatherOf(panel)}
        onPick={(value) => inject({ unsafe: value === 'unsafe', rain: value === 'rain' })}
      />
      <Choice
        title={`Mount: ${MOUNT_TEXT[panel.mount]}`}
        options={[
          { value: 'park', label: 'Park' },
          { value: 'unpark', label: 'Unpark' },
        ]}
        current={panel.mount === 'unparked' ? 'unpark' : 'park'}
        onPick={(value) => inject({ mount: value === 'park' ? 'park' : 'unpark' })}
      />
      <Choice
        title="Park fails"
        options={OFF_ON}
        current={panel.parkFails ? 'on' : 'off'}
        onPick={(value) => inject({ parkFails: value === 'on' })}
      />
      <Choice title="Jam" options={OFF_ON} current={panel.jam ? 'on' : 'off'} onPick={(value) => inject({ jam: value === 'on' })} />
      <Choice
        title="12 V supply"
        options={[
          { value: 'normal', label: 'Normal' },
          { value: 'low', label: 'Low' },
        ]}
        current={panel.supplyLow ? 'low' : 'normal'}
        onPick={(value) => inject({ supplyLow: value === 'low' })}
      />
      <Choice title="Sun up" options={OFF_ON} current={panel.sunUp ? 'on' : 'off'} onPick={(value) => inject({ sunUp: value === 'on' })} />
      <p class="demo-roof-summary" data-inventory="demo-roof-summary">
        Roof {panel.state}; weather {panel.weather}; mount {panel.park}; close sequence {panel.phase}; daylight {panel.daylight}.
      </p>
    </div>
  );
};

export default RoofControls;
