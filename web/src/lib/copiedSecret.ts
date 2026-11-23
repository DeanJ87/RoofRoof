// Spec 024: a secret field set to this takes the value the device received from
// SQMeter in the last copy (held 30 min), only for the server it came with; the
// device swaps it in at the save and never stores or returns it.
export const COPIED_SECRET = '(from SQMeter)';

export const isCopiedSecret = (value: unknown): boolean => value === COPIED_SECRET;

const MASK = '********';

/** After a save: a secret taken from SQMeter is now the device's own, shown masked like any saved secret. */
export const maskCopiedSecrets = <T>(value: T): T => {
  if (isCopiedSecret(value)) return MASK as T;
  if (typeof value !== 'object' || value === null || Array.isArray(value)) return value;
  return Object.fromEntries(Object.entries(value).map(([key, part]) => [key, maskCopiedSecrets(part)])) as T;
};
