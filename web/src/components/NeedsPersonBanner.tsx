import { FunctionalComponent } from 'preact';
import { useEffect, useState } from 'preact/hooks';
import { Banner } from './ui';
import { fetchStatus } from '../lib/settingsApi';
import { t } from '../i18n';

// Spec 030: while the roof needs a person (a restart with the position unknown
// or a fault latched, the Hall sensor failing, the automatic close unable to
// move), every page says so, with the device's own words for what and why.

const POLL_MS = 10000;

export type NeedsPerson = { active: boolean; text: string } | undefined;

/** The live "needs a person" state from the status, read every 10 s. */
export const useNeedsPerson = (): NeedsPerson => {
  const [state, setState] = useState<NeedsPerson>(undefined);
  useEffect(() => {
    let live = true;
    // An error answer (null) keeps the last known state.
    const read = () =>
      fetchStatus()
        .then((status) => live && status && setState(status.roof?.needsPerson))
        .catch(() => undefined);
    read();
    const timer = setInterval(read, POLL_MS);
    return () => {
      live = false;
      clearInterval(timer);
    };
  }, []);
  return state;
};

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
