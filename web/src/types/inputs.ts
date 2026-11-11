// GET /api/roof/inputs (spec 015, specs/015-setup-wizard/contracts/roof-inputs.md):
// the weather sources, the park and N.I.N.A. as the device last evaluated them.

export type SourceState = 'off' | 'waiting' | 'good' | 'stale' | 'unreachable' | 'unreadable' | 'offline';
export type NinaState = 'notConfigured' | 'waiting' | 'good' | 'unreachable' | 'unreadable' | 'apiError';

export interface InputsSource {
  enabled: boolean;
  state: SourceState;
  generation: number;
  ageMs: number;
  limitMs: number;
  safe: boolean;
  flags: number;
  text: string;
}

export interface RoofInputs {
  weather: {
    verdict: 'safe' | 'unsafe' | 'unknown';
    text: string;
    http: InputsSource;
    mqtt: InputsSource;
    rainContact: 'notFitted' | 'dry' | 'raining' | 'unreadable';
  };
  park: { parked: boolean; text: string };
  nina: {
    state: NinaState;
    generation: number;
    connected: boolean;
    atPark: boolean;
    slewing: boolean;
    ageMs: number;
    text: string;
  };
}
