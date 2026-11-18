// GET /api/roof/teach (spec 019, contracts/jog-teach-api.md).

export type TeachPlace = 'closed' | 'open' | 'partWay';
export type JogDirection = 'open' | 'close';

export interface TeachMark {
  count: number;
  mean: number;
  min: number;
  max: number;
  noise: number;
}

export type TeachProposal =
  { ok: true; hallHighEnd: 'open' | 'closed'; hallHighCounts: number; hallLowCounts: number } | { ok: false; reason: string; text: string };

export interface TeachStatus {
  jog: {
    active: boolean;
    id: number;
    direction: JogDirection | null;
    elapsedMs: number;
    limitMs: number;
    holdMs: number;
    keepAliveMs: number;
  };
  rest: { ready: boolean; counts: number | null };
  marks: Record<TeachPlace, TeachMark | null>;
  proposal: TeachProposal | null;
}
