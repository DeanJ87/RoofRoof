import { FunctionalComponent } from 'preact';
import { t } from '../i18n';

// Spec 028: what a live page shows while it has no reading from its current
// connection, on first load ("Connecting") and after a drop ("Reconnecting").
// Nothing from an earlier connection is shown as current. useWebSocket
// announces the drop and the return, so this isn't a live region.
const LiveWaiting: FunctionalComponent<{ reconnecting?: boolean }> = ({ reconnecting }) => (
  <div class="empty-state live-waiting">
    <span class="status-dot" aria-hidden="true" />
    <h2>{reconnecting ? t('live.reconnecting') : t('live.connecting')}</h2>
  </div>
);

export default LiveWaiting;
