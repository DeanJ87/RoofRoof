import { ComponentChildren, FunctionalComponent } from 'preact';
import { t } from '../i18n';

// Spec 028: what a live page shows while it has no reading from its current
// connection, on first load ("Connecting") and after a drop ("Reconnecting").
// Nothing from an earlier connection is shown as current. useWebSocket
// announces the drop and the return, so this isn't a live region.
// Spec 037: refused for the device's page limit, it says so instead.
const LiveWaiting: FunctionalComponent<{ reconnecting?: boolean; tooMany?: boolean }> = ({ reconnecting, tooMany }) => (
  <div class="empty-state live-waiting">
    <span class="status-dot" aria-hidden="true" />
    <h2>{tooMany ? t('live.tooMany') : reconnecting ? t('live.reconnecting') : t('live.connecting')}</h2>
    {tooMany && <p>{t('live.tooManyHint')}</p>}
  </div>
);

// Spec 037: after a drop, the last reading stays on the page, greyed under a
// small badge, until the next one arrives; it is never treated as live (the
// Roof card's Open and Close stay off, Stop stays usable).
export const LiveStale: FunctionalComponent<{ tooMany?: boolean; children: ComponentChildren }> = ({ tooMany, children }) => (
  <div class="live-stale">
    <p class="live-stale-badge">
      <span class="status-dot" aria-hidden="true" />
      {tooMany ? `${t('live.tooMany')}. ${t('live.tooManyHint')}` : t('live.lastKnown')}
    </p>
    <div class="live-stale-body">{children}</div>
  </div>
);

export default LiveWaiting;
