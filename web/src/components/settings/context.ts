import type { Config, SystemStatus } from '../../types';
import type { DepEntry, EffectiveView } from '../../lib/settingsDeps';
import type { SettingsTabId } from './tabs';

export type ConfigPath = string[];

export interface SettingsTabProps {
  config: Config;
  update: (path: ConfigPath, value: unknown) => void;
  // Apply several changes as one state update (avoids lost writes when two
  // fields change together, e.g. hour + minute).
  updateMany: (changes: [ConfigPath, unknown][]) => void;
  error: (key: string) => string | undefined;
  status: SystemStatus | null;
  dirty: boolean; // unsaved changes exist
  goTo: (tab: SettingsTabId, anchor?: string) => void;
  // Whether each dependent setting is in effect (SQMeter spec 020).
  deps: EffectiveView;
  // The one-click fix for an inactive setting: go to its dependency, or restart.
  fix: (entry: DepEntry) => void;
}
