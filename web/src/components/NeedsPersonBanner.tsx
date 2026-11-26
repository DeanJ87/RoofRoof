import { FunctionalComponent } from 'preact';
import { Banner } from './ui';
import { useLiveStatus } from '../hooks/useLiveStatus';
import { t } from '../i18n';

// Spec 030: while the roof needs a person (a restart with the position unknown
// or a fault latched, the Hall sensor failing, the automatic close unable to
// move), every page says so, with the device's own words for what and why.
// Spec 045: from the page's live status; read every 10 s only without it.

const POLL_MS = 10000;

export type NeedsPerson = { active: boolean; text: string } | undefined;

/** The live "needs a person" state: from the live status, or read every 10 s without it (spec 045). */
export const useNeedsPerson = (): NeedsPerson => useLiveStatus((status) => status.roof?.needsPerson, POLL_MS);

const NeedsPersonBanner: FunctionalComponent = () => {
  const state = useNeedsPerson();
  if (!state?.active) return null;
  return (
    <Banner tone="bad" title={t('layout.needsPersonTitle')} inventory="needs-person-banner" className="needs-person-banner">
      {state.text}
    </Banner>
  );
};

export default NeedsPersonBanner;
