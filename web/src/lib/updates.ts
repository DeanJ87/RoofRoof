import { deviceError } from '../i18n/deviceMessage';

// Firmware updates (OTA): uploading an image by hand. RoofRoof has no
// release check; updates are uploaded (spec 001 clarification).

export type UploadHandlers = {
  onProgress: (percent: number) => void;
  onLoad: (status: number, responseText: string) => void;
  onError: () => void;
  /** No upload progress for UPLOAD_STALL_MS: the request was aborted. */
  onStall: () => void;
};

/** An upload that sends nothing for this long has stalled (spec 012). */
export const UPLOAD_STALL_MS = 20_000;

/** POST `file` as the `update` form field with upload progress (fetch can't report it). */
export const uploadImage = (endpoint: string, file: File, handlers: UploadHandlers) => {
  const formData = new FormData();
  formData.append('update', file);
  const xhr = new XMLHttpRequest();
  let stallTimer: ReturnType<typeof setTimeout> | undefined;
  const watch = () => {
    clearTimeout(stallTimer);
    stallTimer = setTimeout(() => {
      xhr.abort();
      handlers.onStall();
    }, UPLOAD_STALL_MS);
  };
  xhr.upload.addEventListener('progress', (e) => {
    watch();
    if (e.lengthComputable) handlers.onProgress(Math.round((e.loaded / e.total) * 100));
  });
  // Sent in full: the device's reply (or its restart) can take a while.
  xhr.upload.addEventListener('load', () => clearTimeout(stallTimer));
  xhr.addEventListener('load', () => {
    clearTimeout(stallTimer);
    handlers.onLoad(xhr.status, xhr.responseText);
  });
  xhr.addEventListener('error', () => {
    clearTimeout(stallTimer);
    handlers.onError();
  });
  xhr.open('POST', endpoint);
  xhr.send(formData);
  watch();
};

/**
 * What an upload's reply means: the device is restarting onto the new image,
 * or it refused (with its reason, or `unknown` when it gave none).
 */
export const uploadOutcome = (status: number, responseText: string, unknown: string): { ok: true } | { ok: false; errorMsg: string } => {
  if (status === 200) {
    try {
      const response = JSON.parse(responseText);
      return response.success === true ? { ok: true } : { ok: false, errorMsg: deviceError(response, unknown) };
    } catch {
      // A 200 that isn't JSON: the device took the image.
      return { ok: true };
    }
  }
  // Failures carry {"error": "..."} with a 4xx/5xx status.
  let errorMsg = `HTTP ${status}`;
  try {
    errorMsg = deviceError(JSON.parse(responseText), errorMsg);
  } catch {
    // not JSON - keep the status code
  }
  return { ok: false, errorMsg };
};

/** Demo mode: fake progress in steps, since the emulated device can't report a real one. */
export const fakeProgress = (onStep: (percent: number) => void, onDone: () => void) => {
  let p = 0;
  const iv = setInterval(() => {
    p = Math.min(p + Math.random() * 12 + 6, 100);
    onStep(Math.round(p));
    if (p >= 100) {
      clearInterval(iv);
      onDone();
    }
  }, 250);
};
