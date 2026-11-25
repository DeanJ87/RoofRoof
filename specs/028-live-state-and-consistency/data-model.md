# Data Model: Honest Live State and Consistent Secondary Text

No stored data changes. Runtime entities:

## Live phase (web, per shared socket)

| Value | Meaning |
|---|---|
| `connecting` (`data` null, `reconnecting` false) | no reading received on this page's socket yet |
| `live` (`data` set) | a reading received on the current connection |
| `reconnecting` (`data` null, `reconnecting` true) | a reading was received before, none on the current connection |

Transitions: `connecting → live` on the first message; `live → reconnecting` on close or after 5 s
quiet; `reconnecting → live` on the next message. A new page (no subscriber left) starts at
`connecting`.

## Live schedule (firmware and demo)

| Field | Value |
|---|---|
| rest interval | 2000 ms |
| moving interval (opening, closing) | 500 ms |
| change interval (status only: state, fault, waiting close, phase) | at most every 250 ms |
| quiet limit (web) | 5000 ms |
| reconnect back-off (web) | 1 s doubling to 30 s, plus 0-1 s jitter |

## Secondary text tokens (CSS)

`--subtext-size`, `--subtext-line`, `--subtext-color`.
