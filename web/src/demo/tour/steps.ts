// The demo tour's steps (SQMeter spec 018 FR-001). Action steps finish when
// the emulated device reacts (FR-002), never on a timer, and offer "Do it for
// me". Spec 011 adds the roof's steps.

export interface TourStep {
  id: string;
  title: string;
  body: () => string;
  /** CSS selector of the control to outline; none = a centred card. */
  target?: string;
  /** Page the step is about (opened when the step starts). */
  route?: string;
  /** Action steps: true once the device has reacted. */
  done?: () => boolean;
  doIt?: () => void;
  doItLabel?: string;
}

export const TOUR_STEPS: TourStep[] = [
  {
    id: 'welcome',
    title: 'This is RoofRoof',
    body: () =>
      "A roll-off roof controller for observatories: it opens and closes the roof, asks the imaging app to park the mount first, and closes the roof when the weather turns. This demo runs the device's own code in your browser.",
  },
  {
    id: 'status',
    title: 'Status',
    route: '/',
    target: '[data-masonry-id="status"]',
    body: () => 'Whether the imaging app is checking the device, and whether alerts go out.',
  },
  {
    id: 'bell',
    title: 'Alerts',
    route: '/',
    target: '.alerts-bell',
    body: () =>
      'Alerts land under the bell. A real RoofRoof sends them to your phone (Pushover, ntfy, MQTT); turn on Real notifications in the Demo panel to try that here.',
  },
  {
    id: 'demo-panel',
    title: 'The Demo panel',
    target: '.demo-panel-toggle',
    body: () => "Speed up the device's clock, try real notifications, and take this tour again.",
  },
];

export const LINKS = [{ label: 'GitHub', href: 'https://github.com/DeanJ87/RoofRoof' }];
