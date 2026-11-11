import { formatMinutes, formatTime } from '../i18n/format';

// A clock time, or "--" without one.
export const formatClock = (date: Date | null) => (date ? formatTime(date) : '--');

// A duration in the UI's words, e.g. "4 min".
export const formatDuration = (ms: number) => formatMinutes(ms / 60000);
