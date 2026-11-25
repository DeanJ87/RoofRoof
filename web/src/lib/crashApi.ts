// Crash capture (spec 029, contracts/crashes.md). Both documents are plain
// text, one fact per line; both need the password when protection is on (the
// browser asks for it). Null when unreachable or refused.
const fetchText = async (url: string): Promise<string | null> => {
  try {
    const response = await fetch(url);
    return response.ok ? await response.text() : null;
  } catch {
    return null;
  }
};

/** GET /api/diag/crashes: this start, then each record. */
export const fetchCrashes = () => fetchText('/api/diag/crashes');

/** GET /api/diag/log: the last log lines. */
export const fetchDeviceLog = () => fetchText('/api/diag/log');

/** The document holds at least one record ("Record <seq>: <kind>" blocks). */
export const hasRecords = (text: string) => /^Record \d+: /m.test(text);
