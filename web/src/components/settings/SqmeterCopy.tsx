import { FunctionalComponent } from 'preact';
import { useState } from 'preact/hooks';
import type { SettingsTabProps } from './context';
import { Toggle } from './controls';
import { Button, Note } from '../ui';
import {
  changesOf,
  copyNotes,
  copyRows,
  fetchSqmeterSite,
  secretsOutcome,
  type CopyNote,
  type CopyRow,
  type CopyRowId,
  type CopySecret,
} from '../../lib/sqmeterCopy';
import { rememberCopy } from '../../lib/syncView';
import type { SqmeterSite } from '../../types';
import { t } from '../../i18n';

// Copy site settings from SQMeter (spec 020 US3, FR-016): fetch, review the
// settings that differ, apply the ticked ones to the page's settings. Nothing
// is saved here: the normal Save keeps them. Settings → Roof and the setup
// wizard's SQMeter screen (which passes the address as typed).

type View =
  | { kind: 'idle' }
  | { kind: 'busy' }
  | { kind: 'failed'; error: string; password?: boolean }
  | { kind: 'review'; rows: CopyRow[]; notes: CopyNote[]; ticked: Set<CopyRowId>; site: SqmeterSite }
  | { kind: 'applied'; secrets: CopySecret[]; notes: CopyNote[] };

const SecretNotes: FunctionalComponent<{ secrets: CopySecret[]; notes: CopyNote[] }> = ({ secrets, notes }) => (
  <>
    {secrets.map((secret) => (
      <Note key={secret} tone="warn">
        {t(`settings.sqmeterCopy.secret.${secret}`)}
      </Note>
    ))}
    {notes.map((note) => (
      <Note key={note}>{t(`settings.sqmeterCopy.note.${note}`)}</Note>
    ))}
  </>
);

// Spec 024: what became of SQMeter's passwords (never their values).
const SecretsNotes: FunctionalComponent<{ site: SqmeterSite }> = ({ site }) => {
  const { received, http, problem } = secretsOutcome(site);
  return (
    <div data-sqmeter-secrets={problem ?? (received ? 'received' : 'none')}>
      {received && <Note tone="ok">{t('settings.sqmeterCopy.secretsReceived')}</Note>}
      {http && <Note>{t('settings.sqmeterCopy.secretsHttp')}</Note>}
      {problem && <Note tone="warn">{t(`sqmeterSecrets.${problem}`)}</Note>}
    </div>
  );
};

const secretsOf = (rows: CopyRow[]): CopySecret[] => [...new Set(rows.flatMap((row) => row.secrets))];

// `beforeFetch` (the wizard): saves the typed address first, as SQMeter's passwords come only from the saved one (spec 024).
type CopyProps = SettingsTabProps & { url?: string; wizard?: boolean; beforeFetch?: () => Promise<boolean> };

export const SqmeterCopy: FunctionalComponent<CopyProps> = ({ config, updateMany, url, wizard, beforeFetch }) => {
  const [view, setView] = useState<View>({ kind: 'idle' });
  const address = (url ?? config.safety?.http.url ?? '').trim();

  const fetchSite = async () => {
    setView({ kind: 'busy' });
    if (beforeFetch && !(await beforeFetch())) return setView({ kind: 'idle' });
    const http = config.safety?.http;
    const answer = await fetchSqmeterSite(url?.trim() || undefined, { username: http?.username, password: http?.password });
    if (!answer.ok) return setView({ kind: 'failed', error: answer.error, password: answer.password });
    const rows = copyRows(answer.site, config);
    setView({ kind: 'review', rows, notes: copyNotes(answer.site), ticked: new Set(rows.map((row) => row.id)), site: answer.site });
  };

  const fetchButton = (
    <div class="btn-row">
      <Button
        onClick={() => void fetchSite()}
        disabled={address === ''}
        busy={view.kind === 'busy'}
        busyLabel={t('settings.sqmeterCopy.fetching')}
      >
        {t('settings.sqmeterCopy.copy')}
      </Button>
    </div>
  );

  if (address === '')
    return (
      <>
        {fetchButton}
        <Note tone="warn">{t('settings.sqmeterCopy.noAddress')}</Note>
      </>
    );

  if (view.kind === 'review') {
    const { rows, notes, ticked, site } = view;
    if (rows.length === 0)
      return (
        <>
          {fetchButton}
          <Note tone="ok">{t('settings.sqmeterCopy.nothing')}</Note>
          <SecretsNotes site={site} />
          <SecretNotes secrets={[]} notes={notes} />
        </>
      );
    const tick = (id: CopyRowId, on: boolean) => {
      const next = new Set(ticked);
      if (on) next.add(id);
      else next.delete(id);
      setView({ ...view, ticked: next });
    };
    const apply = () => {
      const appliedRows = rows.filter((row) => ticked.has(row.id));
      const applied = secretsOf(appliedRows);
      // Secrets SQMeter has set are asked for at their fields, also with the channel off (one that
      // RoofRoof only had gets the "cleared" note instead).
      rememberCopy([
        ...appliedRows.flatMap((row) => row.asks ?? []),
        ...(notes.includes('pushover') ? ['pushoverKeys' as const] : []),
        ...(ticked.has('weatherTopic') ? ['weatherTopic' as const] : []),
      ]);
      updateMany(changesOf(rows, ticked));
      setView({ kind: 'applied', secrets: applied, notes });
    };
    return (
      <div data-inventory="sqmeter-copy-review">
        {rows.map((row) => (
          <div key={row.id}>
            <Toggle
              dataField={`sqmeterCopy.${row.id}`}
              label={t(`settings.sqmeterCopy.row.${row.id}`)}
              checked={ticked.has(row.id)}
              onChange={(on) => tick(row.id, on)}
            />
            <div class="indent">
              <Note>{t('settings.sqmeterCopy.detail', { theirs: row.theirs, ours: row.ours })}</Note>
              {ticked.has(row.id) && <SecretNotes secrets={row.secrets} notes={[]} />}
            </div>
          </div>
        ))}
        <SecretsNotes site={site} />
        <SecretNotes secrets={[]} notes={notes} />
        {config.sqmeterSync && <Note>{t('settings.sqmeterCopy.keepsFollowing')}</Note>}
        <div class="btn-row">
          <Button variant="primary" onClick={apply} disabled={ticked.size === 0}>
            {t('settings.sqmeterCopy.apply')}
          </Button>
          <Button onClick={() => setView({ kind: 'idle' })}>{t('settings.alerts.cancel')}</Button>
        </div>
        {ticked.size === 0 && <Note>{t('settings.sqmeterCopy.noneTicked')}</Note>}
      </div>
    );
  }

  return (
    <>
      {fetchButton}
      {view.kind === 'failed' && (
        <div role="alert">
          <Note tone="bad">{view.error}</Note>
          {view.password && <Note>{t('settings.sqmeterCopy.checkPassword')}</Note>}
        </div>
      )}
      {view.kind === 'applied' && (
        <>
          <Note tone="ok">{t(wizard ? 'settings.sqmeterCopy.appliedWizard' : 'settings.sqmeterCopy.applied')}</Note>
          <SecretNotes secrets={view.secrets} notes={view.notes} />
        </>
      )}
    </>
  );
};

export default SqmeterCopy;
