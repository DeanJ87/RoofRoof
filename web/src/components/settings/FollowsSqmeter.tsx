import { FunctionalComponent } from 'preact';
import type { SyncGroup, SyncNeed } from '../../types';
import type { SettingsTabProps } from './context';
import { Note } from '../ui';
import { follows, fromCopy, keepsSecrets, locked, needs, secretsProblem, syncLine } from '../../lib/syncView';
import { deviceText } from '../../i18n/deviceMessage';
import { t } from '../../i18n';

// Spec 020: a group of site settings that follows SQMeter is read-only here
// (its fields disabled, this note saying why), with "Edit here instead"; a
// group set here can follow SQMeter again once its address is set. Either is
// an edit the normal Save keeps.

type Props = Pick<SettingsTabProps, 'config' | 'status' | 'update'> & { group: SyncGroup };

export const FollowsSqmeter: FunctionalComponent<Props> = ({ group, config, status, update }) => {
  if (!config.sqmeterSync) return null;
  const setFollow = (follow: boolean) => update(['sqmeterSync', group, 'follow'], follow);
  if (!follows(config, group)) {
    if (!config.safety?.http.url.trim()) return null;
    return <Note action={{ label: t('sync.follow'), onClick: () => setFollow(true) }}>{t('sync.local')}</Note>;
  }
  // Without SQMeter's address or held by MQTT roof commands nothing is synced and the fields stay
  // editable; say that what is typed is replaced once it syncs, unless the group is made local.
  if (!locked(config, group)) {
    const why = config.safety?.http.url.trim() ? t('sync.heldRoofCommands') : t('sync.heldNoAddress');
    return (
      <div data-sync={group}>
        <Note tone="warn" action={{ label: t('sync.editHere'), onClick: () => setFollow(false) }}>
          {why}
        </Note>
      </div>
    );
  }
  return <Followed group={group} status={status} onLocal={() => setFollow(false)} />;
};

// A followed group's line, the device's refusal, and (spec 024) the passwords it keeps or couldn't read.
const Followed: FunctionalComponent<Pick<Props, 'group' | 'status'> & { onLocal: () => void }> = ({ group, status, onLocal }) => {
  const line = syncLine(status?.sync, group);
  const refused = status?.sync?.[group]?.follow ? status.sync[group].refused : null;
  const secrets = secretsProblem(status?.sync, group);
  return (
    <div data-sync={group}>
      <Note tone={line.problem ? 'warn' : 'muted'} action={{ label: t('sync.editHere'), onClick: onLocal }}>
        {line.text}
      </Note>
      {refused && <Note tone="warn">{deviceText(refused)}</Note>}
      {/* Spec 024: the passwords and switches follow too (switches only ever turn on). */}
      {keepsSecrets(group) && <Note tone="muted">{t('sync.keepsSecrets')}</Note>}
      {secrets && <Note tone="warn">{t(`sqmeterSecrets.${secrets}`)}</Note>}
    </div>
  );
};

// At a secret's field: SQMeter has it set and RoofRoof hasn't (e.g. after a synced broker change cleared it),
// as the device says for a following group, or as a copy found it until the field is `filled`.
// `cleared`: the form's value is null (a copied broker or ntfy server cleared it), so the old one won't be sent.
type NeedsProps = Pick<SettingsTabProps, 'status'> & { need: SyncNeed; filled: boolean; cleared?: boolean };
export const NeedsNote: FunctionalComponent<NeedsProps> = ({ status, need, filled, cleared }) => {
  if (needs(status?.sync, need) || (fromCopy(need) && !filled)) return <Note tone="warn">{t(`sync.needs.${need}`)}</Note>;
  if (cleared && need !== 'pushoverKeys') return <Note tone="warn">{t(`sync.cleared.${need}`)}</Note>;
  return null;
};
