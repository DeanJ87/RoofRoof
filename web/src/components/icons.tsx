import { ComponentChildren, FunctionalComponent } from 'preact';

// The design system's icons (ui.tsx re-exports them).

export const COLORS = {
  cyan: '#55c7f2',
  violet: '#a78bfa',
  green: '#5ee0a0',
  amber: '#f4c15d',
  red: '#f26b62',
  muted: '#566675',
};

export type IconTone = keyof typeof COLORS;

export const Icon: FunctionalComponent<{ name: string; tone?: IconTone; size?: number }> = ({ name, tone = 'cyan', size = 15 }) => {
  const common = {
    stroke: COLORS[tone],
    strokeWidth: 1.7,
    fill: 'none',
    strokeLinecap: 'round' as const,
    strokeLinejoin: 'round' as const,
  };
  const paths: Record<string, ComponentChildren> = {
    star: <path {...common} d="m12 3 2.6 5.5 6 .8-4.4 4.2 1.1 6-5.3-2.9-5.3 2.9 1.1-6-4.4-4.2 6-.8L12 3Z" />,
    cloud: <path {...common} d="M18 18H8.2a4.2 4.2 0 1 1 1.1-8.2 6 6 0 0 1 11 3.3A2.8 2.8 0 0 1 18 18Z" />,
    therm: (
      <>
        <path {...common} d="M10 14.5V5a2 2 0 1 1 4 0v9.5a4 4 0 1 1-4 0Z" />
        <path {...common} d="M12 7v8" />
      </>
    ),
    gps: (
      <>
        <circle {...common} cx="12" cy="12" r="3.2" />
        <path {...common} d="M12 3v3M12 18v3M3 12h3M18 12h3" />
      </>
    ),
    eye: (
      <>
        <path {...common} d="M3 12s3.3-6 9-6 9 6 9 6-3.3 6-9 6-9-6-9-6Z" />
        <circle {...common} cx="12" cy="12" r="2.4" />
      </>
    ),
    wifi: (
      <>
        <path {...common} d="M5 10a10 10 0 0 1 14 0M8 13a6 6 0 0 1 8 0M11 16a2 2 0 0 1 2 0" />
        <path {...common} d="M12 19h.01" />
      </>
    ),
    rain: (
      <>
        <path {...common} d="M18 15H8.4a3.6 3.6 0 0 1 .9-7 5.2 5.2 0 0 1 9.5 2.9A2.4 2.4 0 0 1 18 15Z" />
        <path {...common} d="M8 19v1M12 18v2M16 19v1" />
      </>
    ),
    cpu: (
      <>
        <rect {...common} x="5" y="5" width="14" height="14" rx="2" />
        <path {...common} d="M9 1v4M15 1v4M9 19v4M15 19v4M1 9h4M1 15h4M19 9h4M19 15h4" />
      </>
    ),
    upload: (
      <>
        <path {...common} d="M12 16V4M7 9l5-5 5 5" />
        <path {...common} d="M5 18v2h14v-2" />
      </>
    ),
    moon: <path {...common} d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5Z" />,
    roof: <path {...common} d="M3 11 12 4l9 7M6 9.5V20h12V9.5M10 20v-5h4v5" />,
    // Spec 018: the wizard's header and choices.
    back: <path {...common} d="M19 12H5m6-6-6 6 6 6" />,
    close: <path {...common} d="M6 6l12 12M18 6 6 18" />,
    lock: (
      <>
        <rect {...common} x="5" y="11" width="14" height="10" rx="2" />
        <path {...common} d="M8 11V8a4 4 0 0 1 8 0v3" />
      </>
    ),
    off: (
      <>
        <circle {...common} cx="12" cy="12" r="8" />
        <path {...common} d="m6.5 6.5 11 11" />
      </>
    ),
    bolt: <path {...common} d="M13 3 5 14h6l-1 7 8-11h-6l1-7Z" />,
    switch: (
      <>
        <rect {...common} x="3" y="8" width="18" height="8" rx="4" />
        <circle {...common} cx="16" cy="12" r="2" />
      </>
    ),
    magnet: <path {...common} d="M6 4v8a6 6 0 0 0 12 0V4h-4v8a2 2 0 0 1-4 0V4H6ZM6 8h4m4 0h4" />,
    link: <path {...common} d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1" />,
    bell: <path {...common} d="M6 16V11a6 6 0 0 1 12 0v5l2 2H4l2-2Zm4 4h4" />,
    sun: (
      <>
        <circle {...common} cx="12" cy="12" r="4" />
        <path {...common} d="M12 2v2m0 16v2M2 12h2m16 0h2M5 5l1.5 1.5m11 11L19 19M5 19l1.5-1.5m11-11L19 5" />
      </>
    ),
    scope: <path {...common} d="m3 14 13-7 2 4-13 7-2-4Zm7 .5L8 21m2-6.5 4 6.5" />,
  };

  return (
    <svg class="ui-icon" width={size} height={size} viewBox="0 0 24 24" aria-hidden="true">
      {paths[name] ?? null}
    </svg>
  );
};
