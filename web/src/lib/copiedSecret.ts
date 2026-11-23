// Spec 024: a secret field set to this takes the value the device received from
// SQMeter in the last copy (held 30 min), only for the server it came with; the
// device swaps it in at the save and never stores or returns it.
export const COPIED_SECRET = '(from SQMeter)';

export const isCopiedSecret = (value: unknown): boolean => value === COPIED_SECRET;
