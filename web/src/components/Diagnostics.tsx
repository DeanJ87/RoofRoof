import { FunctionalComponent } from 'preact';
import { useEffect, useMemo, useRef, useState } from 'preact/hooks';
import { route } from 'preact-router';
import type { DiagDocument, DiagLive, DiagMove, DiagMoves, DiagPoint, DiagSummary } from '../types/diag';
import { useWebSocket } from '../hooks/useWebSocket';
import { clearMoves, fetchDiag, fetchFile, fetchMove, fetchMoves, moveUrl } from '../lib/diagApi';
import { LiveBuffer } from '../lib/diagChart';
import { bodyOf } from '../lib/api';
import { Button, Card, Note, Pill, ReadingRow } from './ui';
import DiagChart from './diagnostics/DiagChart';
import { ControlCard, DeviceCard, MemoryCard, RoofCard, SupplyCard, TimingCard, roofState, seconds } from './diagnostics/LiveCards';
import { t, tMaybe } from '../i18n';
import { deviceError } from '../i18n/deviceMessage';
import { formatCount, formatDateTime, formatNumber } from '../i18n/format';

// Advanced diagnostics (spec 017 FR-032, FR-033): live tiles from /ws/diag,
// the move in progress or a chosen move as a chart, the move list with its
// downloads, and Clear log. With the mode off the page says so and opens no
// stream. Stop stays in the Layout's bar.

const isDemo = import.meta.env.VITE_DEMO_MODE === 'true';
const DASH = '--';

const moving = (live: DiagLive) => live.roof.state === 'opening' || live.roof.state === 'closing';

const num = (value: number | null | undefined, digits: number) => formatNumber(value, digits);

/** Opens the diagnostics settings (Settings → Device → Diagnostics). */
const toSettings = () => route('/settings?tab=device&section=diagnostics');

export const DiagOffNote: FunctionalComponent = () => (
  <Note tone="warn" action={{ label: t('diagnostics.turnOn'), onClick: toSettings }}>
    {t('diagnostics.offNote')}
  </Note>
);

// The device sends the CSV as an attachment. The demo answers in the page, so
// it saves what its handler returns.
const saveInDemo = (event: MouseEvent, url: string, name: string) => {
  if (!isDemo) return;
  event.preventDefault();
  void fetchFile(url).then((blob) => {
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = name;
    link.click();
    setTimeout(() => URL.revokeObjectURL(link.href), 1000);
  });
};

export const Downloads: FunctionalComponent<{ id: number }> = ({ id }) => (
  <div class="btn-row">
    {[true, false].map((csv) => {
      const url = moveUrl(id, csv);
      const name = `roofroof-move-${id}.${csv ? 'csv' : 'json'}`;
      return (
        // `native`: a file from the device, not a page of this app (preact-router leaves it alone).
        <a
          key={name}
          class="btn btn-default btn-sm"
          href={url}
          download={name}
          {...{ native: '' }}
          onClick={(event) => saveInDemo(event, url, name)}
        >
          {csv ? t('diagnostics.downloadCsv') : t('diagnostics.downloadJson')}
        </a>
      );
    })}
  </div>
);

// --- Moves ------------------------------------------------------------------------

const RESULT_PILL = { arrived: 'pill-green', stopped: 'pill-amber', fault: 'pill-red' } as const;

export const ResultPill: FunctionalComponent<{ move: DiagSummary }> = ({ move }) =>
  move.active ? (
    <Pill tone="pill-cyan">{t('diagnostics.result.active')}</Pill>
  ) : (
    <Pill tone={RESULT_PILL[move.result]}>{t(`diagnostics.result.${move.result}`)}</Pill>
  );

const when = (move: DiagSummary) =>
  move.startEpoch > 0 ? formatDateTime(new Date(move.startEpoch * 1000), { dateStyle: 'short', timeStyle: 'short' }) : DASH;

const target = (move: DiagSummary) => roofState(move.target);

export const MoveList: FunctionalComponent<{
  moves: DiagSummary[];
  selected: number | null;
  onSelect: (id: number) => void;
}> = ({ moves, selected, onSelect }) =>
  moves.length === 0 ? (
    <Note>{t('diagnostics.noMoves')}</Note>
  ) : (
    <div class="diag-table-wrap">
      <table class="diag-table">
        <thead>
          <tr>
            <th scope="col">{t('diagnostics.move')}</th>
            <th scope="col">{t('diagnostics.started')}</th>
            <th scope="col">{t('diagnostics.origin')}</th>
            <th scope="col">{t('diagnostics.target')}</th>
            <th scope="col">{t('diagnostics.resultLabel')}</th>
            <th scope="col">{t('diagnostics.duration')} (s)</th>
            <th scope="col">{t('diagnostics.peak')} (A)</th>
            <th scope="col">{t('diagnostics.average')} (A)</th>
            <th scope="col">{t('diagnostics.minSupply')} (V)</th>
            <th scope="col">{t('diagnostics.sag')} (V)</th>
          </tr>
        </thead>
        <tbody>
          {moves.map((move) => (
            <tr key={move.id} class={move.id === selected ? 'is-selected' : ''}>
              <td>
                {move.profile ? (
                  <Button variant="link" small onClick={() => onSelect(move.id)} ariaLabel={t('diagnostics.showMove', { id: move.id })}>
                    {formatCount(move.id)}
                  </Button>
                ) : (
                  <>
                    {formatCount(move.id)} {move.kept && <span class="system-subtle">{t('diagnostics.kept')}</span>}{' '}
                    {move.calibration && <span class="system-subtle">{t('diagnostics.calibrationLeg')}</span>}{' '}
                    {move.calibration && <span class="system-subtle">{t('diagnostics.calibrationLeg')}</span>}
                  </>
                )}
              </td>
              <td>{when(move)}</td>
              <td>{t(`diagnostics.originName.${move.origin}`)}</td>
              <td>{target(move)}</td>
              <td>
                <ResultPill move={move} />
              </td>
              <td class="ltr">{seconds(move.durationMs)}</td>
              <td class="ltr">{num(move.peakAmps, 2)}</td>
              <td class="ltr">{num(move.averageAmps, 2)}</td>
              <td class="ltr">{num(move.minVolts, 2)}</td>
              <td class="ltr">{num(move.sagVolts, 2)}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );

const faultText = (move: DiagSummary) => (move.fault ? (tMaybe(`diagnostics.fault.${move.fault.kind}`) ?? move.fault.kind) : DASH);

const MoveSummary: FunctionalComponent<{ move: DiagSummary }> = ({ move }) => (
  <div>
    <ReadingRow
      label={t('diagnostics.resultLabel')}
      value={move.active ? t('diagnostics.result.active') : t(`diagnostics.result.${move.result}`)}
    />
    {move.fault && <ReadingRow label={t('diagnostics.faultLabel')} value={faultText(move)} />}
    {move.fault && (
      <ReadingRow
        label={t('diagnostics.faultMeasured')}
        value={t('diagnostics.measuredOfLimit', { measured: num(move.fault.measured, 2), limit: num(move.fault.limit, 2) })}
      />
    )}
    <ReadingRow label={t('diagnostics.duration')} value={`${seconds(move.durationMs)} s`} />
    <ReadingRow label={t('diagnostics.driven')} value={`${seconds(move.drivenMs)} s`} />
    <ReadingRow label={t('diagnostics.stallCurrent')} value={`${num(move.stallAmps, 2)} A`} />
    <ReadingRow label={t('diagnostics.restSupply')} value={`${num(move.restVolts, 2)} V`} />
    <ReadingRow
      label={t('diagnostics.hallStartEnd')}
      value={`${formatCount(move.hallStart)} → ${formatCount(move.hallEnd)}`}
      valueClass="ltr"
    />
  </div>
);

// While the roof moves, Clear log is refused; the reason sits under it (constitution VII).
const ClearLog: FunctionalComponent<{ busy: boolean; onCleared: () => void }> = ({ busy, onCleared }) => {
  const [outcome, setOutcome] = useState<{ ok: boolean; text: string } | null>(null);
  const [sending, setSending] = useState(false);
  const clear = async () => {
    setSending(true);
    try {
      const response = await clearMoves();
      const body = await bodyOf(response);
      setOutcome(
        response.ok ? { ok: true, text: t('diagnostics.cleared') } : { ok: false, text: deviceError(body, t('diagnostics.clearFailed')) },
      );
      if (response.ok) onCleared();
    } catch {
      setOutcome({ ok: false, text: t('diagnostics.clearFailed') });
    }
    setSending(false);
  };
  return (
    <div>
      <div class="btn-row">
        <Button
          variant="danger"
          small
          disabled={busy}
          busy={sending}
          onClick={() => void clear()}
          describedBy={busy ? 'diag-clear-why' : undefined}
        >
          {t('diagnostics.clearLog')}
        </Button>
      </div>
      {busy && <Note id="diag-clear-why">{t('diagnostics.clearWhileMoving')}</Note>}
      {!busy && outcome && <Note tone={outcome.ok ? 'ok' : 'bad'}>{outcome.text}</Note>}
    </div>
  );
};

// The moves list, fetched again when a move starts or ends; a finished move
// reaches the list from the main loop, so it is asked for until it is there.
const useMoves = (live: DiagLive) => {
  const [moves, setMoves] = useState<DiagMoves | null>(null);
  const tries = useRef(0);
  const latest = useRef(moves);
  latest.current = moves;
  const key = live.move ? `${live.move.id}:${live.move.active}` : '';
  const reload = () => void fetchMoves().then(setMoves);
  useEffect(() => {
    tries.current = 0;
    reload();
  }, [key]);
  useEffect(() => {
    const last = live.move;
    const list = latest.current;
    if (!list || !last || last.active || tries.current >= 5) return;
    if (!list.moves.some((m) => m.id === last.id && !m.active)) {
      tries.current += 1;
      reload();
    }
  }, [live]);
  return { moves, reload };
};

interface ChartSource {
  points: DiagPoint[];
  title: string;
  summary: DiagSummary | undefined;
}

// What the chart shows: the stream's move, or the move fetched from the list.
const chartSource = (
  live: DiagLive,
  buffer: LiveBuffer,
  moves: DiagMoves | null,
  wanted: number | null,
  chosen: DiagMove | null,
): ChartSource => {
  if (wanted !== null) return { points: chosen?.points ?? [], title: t('diagnostics.moveNumber', { id: wanted }), summary: chosen?.move };
  const id = buffer.moveId;
  if (id === null) return { points: [], title: t('diagnostics.profile'), summary: undefined };
  const inProgress = live.move?.active && live.move.id === id;
  return {
    points: buffer.points,
    title: t(inProgress ? 'diagnostics.moveInProgress' : 'diagnostics.moveNumber', { id }),
    summary: moves?.moves.find((m) => m.id === id && !m.active),
  };
};

const ChartCard: FunctionalComponent<{ live: DiagLive; buffer: LiveBuffer; moves: DiagMoves | null; selected: number | null }> = ({
  live,
  buffer,
  moves,
  selected,
}) => {
  const [chosen, setChosen] = useState<DiagMove | null>(null);
  // A chosen move, else the stream's move, else the newest move with a profile.
  const fallback = buffer.points.length > 1 ? null : (moves?.moves.find((m) => m.profile && !m.active)?.id ?? null);
  const wanted = selected ?? fallback;
  useEffect(() => {
    setChosen(null);
    if (wanted !== null) void fetchMove(wanted).then(setChosen);
  }, [wanted]);
  const { points, title, summary } = chartSource(live, buffer, moves, wanted, chosen);
  return (
    <Card title={title} icon="therm" hint={t('diagnostics.profileHint')} actions={summary ? <ResultPill move={summary} /> : undefined}>
      <DiagChart points={points} currentSense={live.roof.currentSense} />
      {summary && <MoveSummary move={summary} />}
      {summary?.profile && <Downloads id={summary.id} />}
    </Card>
  );
};

// --- The page ---------------------------------------------------------------------

const LivePage: FunctionalComponent<{ initial: DiagLive }> = ({ initial }) => {
  const { data } = useWebSocket<DiagDocument>('/ws/diag');
  const live = data?.enabled ? data : initial;
  const buffer = useMemo(() => new LiveBuffer(), []);
  const [, setVersion] = useState(0);
  const [selected, setSelected] = useState<number | null>(null);
  const { moves, reload } = useMoves(live);
  const startedId = live.move?.active ? live.move.id : null;
  useEffect(() => {
    if (buffer.add(live)) setVersion((v) => v + 1);
  }, [live, buffer]);
  // A new move takes the chart back to the stream.
  useEffect(() => {
    if (startedId !== null) setSelected(null);
  }, [startedId]);
  return (
    <div class="panel-page diag-page page-enter">
      {live.log === 'unavailable' && <Note tone="bad">{t('diagnostics.logUnavailable')}</Note>}
      <RoofCard live={live} />
      <SupplyCard live={live} />
      <TimingCard live={live} />
      <ChartCard live={live} buffer={buffer} moves={moves} selected={selected} />
      <Card
        title={t('diagnostics.moves')}
        icon="roof"
        hint={t('diagnostics.movesHint', { moves: moves?.maxMoves ?? 5, summaries: moves?.maxSummaries ?? 32 })}
      >
        <MoveList moves={moves?.moves ?? []} selected={selected} onSelect={setSelected} />
        <ClearLog
          busy={moving(live)}
          onCleared={() => {
            setSelected(null);
            buffer.points = [];
            reload();
          }}
        />
      </Card>
      <ControlCard live={live} />
      <DeviceCard live={live} />
      <MemoryCard live={live} />
    </div>
  );
};

const Diagnostics: FunctionalComponent = () => {
  const [doc, setDoc] = useState<DiagDocument | null | undefined>(undefined);
  useEffect(() => {
    void fetchDiag().then(setDoc);
  }, []);
  if (doc === undefined)
    return (
      <div class="empty-state">
        <h2>{t('common.loading')}</h2>
      </div>
    );
  if (doc?.enabled) return <LivePage initial={doc} />;
  return (
    <div class="panel-page page-enter">
      <Card title={t('diagnostics.title')} icon="cpu">
        {doc ? <DiagOffNote /> : <Note tone="bad">{t('diagnostics.unreachable')}</Note>}
      </Card>
    </div>
  );
};

export default Diagnostics;
