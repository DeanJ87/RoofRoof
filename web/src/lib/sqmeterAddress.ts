import { parseHttpUrl } from './netAddress';
import { t } from '../i18n';

// Spec 020 FR-036: SQMeter's address as the device keeps it (lib/ConfigModel
// ConfigSqmeter.cpp): scheme://host[:port]. No scheme means http://; a known
// SQMeter API path, a query and a trailing "/" are dropped; any other path stays
// (and is refused). RoofRoof adds /api/safety and the site paths itself.

const SQMETER_PATHS = ['/api/safety', '/api/sensors', '/api/status', '/api/config'];

export const sqmeterAddress = (typed: string): string => {
  const text = typed.trim();
  if (text === '') return '';
  const separator = text.indexOf('://');
  const scheme = separator === -1 ? 'http' : text.slice(0, separator).toLowerCase();
  const rest = separator === -1 ? text : text.slice(separator + 3);
  const pathStart = rest.search(/[/?#]/);
  const authority = pathStart === -1 ? rest : rest.slice(0, pathStart);
  const path = (pathStart === -1 ? '' : rest.slice(pathStart)).split(/[?#]/)[0].replace(/\/+$/, '');
  const known = path === '' || SQMETER_PATHS.includes(path.toLowerCase());
  return `${scheme}://${authority}${known ? '' : path}`;
};

/** Why the typed address can't be SQMeter's, or null (an empty one is "not set", decided elsewhere). */
export const sqmeterAddressError = (typed: string): string | null => {
  const kept = sqmeterAddress(typed);
  if (kept === '') return null;
  const parsed = parseHttpUrl(kept);
  if (!parsed.url) return parsed.error;
  return parsed.url.path === '/' ? null : t('validation.configSchema.sqmeterAddressOnly');
};

/** FR-039: an https:// SQMeter (encrypted, its certificate not checked). */
export const sqmeterHttps = (typed: string): boolean => sqmeterAddress(typed).startsWith('https://');
