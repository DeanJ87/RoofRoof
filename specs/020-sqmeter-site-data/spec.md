# Feature Specification: Site Data from SQMeter

**Feature Branch**: `spec/020-sqmeter-site-data`

**Created**: 2026-11-19

**Status**: Draft

## Context

RoofRoof and SQMeter stand at the same observatory, so they share a site: the latitude and
longitude, the time zone and NTP servers, the MQTT broker, and usually the alert channels. Today
the owner types each of these into both devices. RoofRoof already knows where SQMeter is: its
weather source `safety.httpUrl` is SQMeter's `GET /api/safety`.

SQMeter serves two documents without a password:

- `GET /api/status`: its `sky` section has the site in use (`latitude`, `longitude`, and
  `locationSource`: `manual`, `gps` or `none`). With a GPS receiver it is a live fix.
- `GET /api/config`: every setting, with each secret replaced by a mask.

This spec makes RoofRoof take its site from SQMeter in three ways:

1. **The location follows SQMeter.** A new setting `location.source` is `sqmeter` or `manual`.
   With `sqmeter`, RoofRoof reads SQMeter's location every ten minutes and keeps the last good one
   in its settings, so the daylight failsafe (spec 013) keeps working while SQMeter is away. The
   dashboard's daylight row and Settings say where the location came from and how old it is.
2. **Copy from SQMeter, once.** In Settings and in the setup wizard, RoofRoof fetches SQMeter's
   settings and lists what would change: the time zone and NTP servers, the MQTT broker, port and
   user name, SQMeter's MQTT base topic (for RoofRoof's MQTT weather source), and the ntfy server
   and topic. The person ticks what to take, and saves through the normal settings save.
   Passwords, tokens and keys are never copied: SQMeter masks them, and the list says which ones
   the person must type in.

3. **The shared settings keep following SQMeter.** Each shareable group (time; MQTT broker and
   SQMeter's base topic; the ntfy alert channel) has a "follows SQMeter" switch
   (`sqmeterSync.<group>.follow`). A followed group is re-read from SQMeter's settings on the
   location's schedule and changed through the normal validation and save when SQMeter's values
   change. Settings and the wizard show a followed group read-only ("Follows SQMeter"), with "Edit
   here instead" to make it local again. Secrets still never sync: a synced server change clears the
   matching saved secret and RoofRoof asks for it.

The demo simulates SQMeter's two documents, so all three work there (spec 011).

## Clarifications

### Session 2026-11-19

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: Where is the location read from? → A: SQMeter's `GET /api/status`, the `sky` section: it is
  the location SQMeter itself uses, including a live GPS fix, which `GET /api/config` doesn't
  have. The address is the scheme, host and port of `safety.httpUrl`, with the path
  `/api/status`. The weather source doesn't have to be on; the address must be set.
- Q: How often, and on which task? → A: On the safety poller's task (spec 004), which already
  talks to SQMeter: every 10 minutes after a good read, 2 minutes after a failed one, and soon
  after a restart or a change of the address or the source. A read runs only in a pass of the
  poller where SQMeter's safety poll does not run, and it is bounded to 3 s (the safety poll is
  bounded to 6 s), so the poller's longest pass is no longer than before. It is skipped while
  the roof moves, a close waits or the automatic close is working (spec 010).
- Q: Where is the last good location kept? → A: In the settings themselves (`location.latitude`,
  `location.longitude`, `location.set`), with two fields the device owns: whether SQMeter's fix
  was GPS, and when the value was saved. The daylight failsafe reads the settings as before, so
  nothing in spec 013 changes. The device saves only when the location moves by 0.001° or more
  (about 110 m), when the GPS flag changes, or when no location was set: a GPS fix that jitters in
  its last digit never writes the flash. The save goes through spec 010's flash-write guard and
  waits while the roof moves or a close waits.
- Q: What happens while SQMeter can't be read? → A: The kept location stays and daylight carries
  on with it. The status says what failed ("SQMeter not answering", "SQMeter has no location")
  and when the location was last read or saved. No alert: the location is not a safety input.
  With no kept location, daylight behaves exactly as today with the location not set.
- Q: Can SQMeter's location change the weather verdict or the park? → A: No. It is read only for
  the sun's altitude. It never enters the weather verdict, the park check or any refusal other
  than through daylight, exactly as a typed-in location does.
- Q: What are the defaults, and what does an upgrade get? → A: A new device: `sqmeter` (it does
  nothing until SQMeter's address is set). Settings saved before this spec: a location that was
  set stays `manual`, with the same value, so nothing changes for anyone who typed one in; settings
  with no location set become `sqmeter`. Any settings document without `location.source` follows
  the same rule.
- Q: Who owns the location fields while the source is `sqmeter`? → A: The device, once it keeps
  one. A settings save that keeps the source on `sqmeter` keeps the device's location, whatever latitude and longitude
  the page sent, so a page opened before the last read can't put an older value back. Switching
  to `manual` takes the latitude and longitude sent with it (the page shows the kept value, so
  switching keeps it unless the person edits it).
- Q: How is SQMeter's settings document fetched for the copy? → A: By the device, on request.
  SQMeter sends no CORS headers, so a browser on RoofRoof's page can't read it. `POST
  /api/sqmeter/site` (password-protected: it makes the device send a request) takes an optional
  address (so the wizard can copy before saving it) and answers with a short site document. The
  fetch runs on its own short-lived task, never on the web server's task, the poller or the
  motion task, and is bounded to 5 s. It is refused while the roof moves, a close waits or the
  automatic close works, and while another copy runs.
- Q: What does the site document carry? → A: Only what the copy can use: SQMeter's name, its
  location, time zone and NTP servers, MQTT broker, port, user name and base topic,
  ntfy server and topic, and for every secret only whether SQMeter has one set (MQTT password,
  ntfy token, Pushover user key and app token). The device never forwards a secret's value, even
  from an SQMeter old enough not to mask it. An older SQMeter that lacks a part leaves that part
  out; v0.1's top-level `timezone` is read as the time zone.
- Q: What is offered, and what never is? → A: Offered, each with SQMeter's value beside
  RoofRoof's: the time zone; the NTP servers; the MQTT broker, port and user name; SQMeter's MQTT
  base topic as RoofRoof's MQTT weather topic (`safety.mqttTopic`; RoofRoof's own base topic stays
  its own); the ntfy server and topic; following SQMeter's location. Never: the device name and
  host name (they must differ), Wi-Fi, any password, token or key, Alpaca, OTA, and the on/off
  switches of MQTT and the alert channels (those stay the person's choice on their own screens).
- Q: How are secrets handled when a copied field changes where a secret is sent? → A: Copying a
  different MQTT broker, port or user name clears RoofRoof's saved MQTT password, and copying a
  different ntfy server clears the saved ntfy token, so a saved secret is never sent to a server it
  wasn't saved for. The list flags each secret the person must type ("Enter the MQTT password").
  Pushover is never copied (its user key is masked); when SQMeter uses it, the list says to enter
  the user key and app token.
- Q: How does the copy reach the settings? → A: The ticked rows change the page's settings, as
  typing would, and the person saves with the normal Save (the device's own validation, refused
  while the roof moves). Nothing is saved by the fetch.
- Q: Where is it in the wizard? → A: A new first screen on the Site and time page asks for
  SQMeter's address, with "Copy site settings from SQMeter". The address is the weather source's
  address, so the later Weather screens arrive filled in, and the Site, Time and Alerts screens
  show what was copied. The page count and `setup.step` don't change. The wizard still offers no
  roof command.
### Session 2026-11-22 (keeping in step)

- Q: Which groups can follow SQMeter? → A: Time (`ntp.timezone`, `ntp.server1`, `ntp.server2`);
  MQTT (`mqtt.broker`, `mqtt.port`, `mqtt.username`, and SQMeter's base topic as
  `safety.mqttTopic`); alerts (`alerts.ntfy.server`, `alerts.ntfy.topic`); and the location
  (`location.source`, unchanged). The Pushover user key is masked by SQMeter, so it can't follow;
  RoofRoof's own MQTT topic, MQTT on/off, the channels' on/off, the device and host names, WiFi,
  Alpaca and everything about motion, the weather verdict and the park never follow.
- Q: What are the defaults? → A: A new device follows SQMeter in every group (it does nothing until
  SQMeter's address is set). Settings saved before this load each group as local when its values
  were already changed from the defaults, else following. Copying a group in Settings or the
  wizard switches it to follow.
- Q: When is SQMeter re-read? → A: On the location's schedule and rules, with its own times: 15 s
  after a start or a change, then every 10 minutes (2 after a failure), at most one SQMeter read in
  a poller pass, never in a pass that polls SQMeter's safety or within 3 s of it, never while the
  roof moves, a close waits or the automatic close works. The settings are read through the same
  filter as the copy, so only the few fields used are kept, however long SQMeter's alert templates.
- Q: How is a change applied? → A: On the loop, under the settings save lock (try-lock, never
  waiting), through the flash-write guard: each followed group takes SQMeter's values on a copy of
  the live settings, which must pass the same validation as a save from the UI (host and URL forms
  included). A group that fails keeps its old values and its status shows why; the others still
  apply. A change is saved once and applied as any save is (MQTT reconnects to a new broker, NTP
  resyncs), and the device logs which groups changed. Values that already match write nothing; a
  group's first confirmation records when it was synced (one write).
- Q: Who owns a followed group's fields? → A: The device, once it has synced the group: a settings
  save that keeps the group following keeps the live values (checked again under the save lock, so
  a staged restore or an older page can't put older ones back). "Edit here instead" (follow off)
  takes the sent values. A group never synced takes the sent values (so the wizard's copy stands).
- Q: What about secrets? → A: Never synced. A synced change of the MQTT broker, port or user name
  clears the saved MQTT password; of the ntfy server, the saved token. While SQMeter has a secret set
  and RoofRoof's is empty, the status names it (`needs`), and Settings and the dashboard's Status
  card show a plain note asking for it. It is not an alert and never a safety event.
- Q: Can a synced broker change who may move the roof? → A: Not without a person. While MQTT roof
  commands are on, the MQTT group isn't synced: its status says "MQTT roof commands are on: set the
  broker here", and the old broker stays.
- Q: What about SQMeter's language and its night threshold? → A: Not offered. RoofRoof ships in
  English only, and SQMeter's `alerts.nightSunAltitudeDeg` means "night below", which is not
  RoofRoof's daylight threshold ("close at or above").

## Constitution rules this spec relies on

| Principle | Rule | Where it is kept |
|---|---|---|
| I | Motion control runs in its own task, independent of the network; a busy network can never prolong a move | FR-004, FR-005: the reads run on the poller task and a short-lived job task, never the motion task; none of them is waited for by a move |
| I | Settings that change motion are refused while moving | FR-007, FR-016: the location save waits under the flash-write guard; the copy saves through the normal save |
| III | Alerts, MQTT and the UI never change the verdict; doubt is unsafe | FR-009: SQMeter's location never enters the weather verdict or the park; FR-029: the sync never writes a verdict, and a synced weather topic restarts that source as unknown (not safe) |
| III | Refusals and problems state the input that failed | FR-010, FR-013 |
| VI | Request handlers never block on the network; outgoing HTTP on its own task | FR-012: the copy's fetch runs on its own task with a paused request |
| VI | Persisted config validated before saving, within NVS limits | FR-006, FR-007 |
| VIII | Endpoints that send something call `requireAuth`; secrets masked and never leaked; MQTT commands only from the configured broker | FR-011, FR-014, FR-015; FR-027: no secret syncs, a synced server clears the saved secret, and the MQTT group never syncs while MQTT roof commands are on |
| IX | Docs move with behaviour | FR-022 |
| XI | SQMeter-specific features (GPS among them) are not carried over | RoofRoof has no GPS of its own: it reads the location SQMeter uses and only records that it was SQMeter's GPS fix |
| Compatibility | Old config loads without a reset; a migration never turns on a motion feature that was off | FR-006 |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - The location follows SQMeter (Priority: P1)

The owner has SQMeter with a GPS receiver beside the roof. RoofRoof's weather source already points
at it. With the location source on "From SQMeter", RoofRoof reads SQMeter's location, keeps it, and
the daylight failsafe uses it. The dashboard's daylight row says "Location from SQMeter (GPS), read
3 min ago".

**Why this priority**: Daylight protection depends on a location, and a forgotten location leaves
it doing nothing. Taking it from SQMeter removes that gap.

**Independent Test**: With a simulated SQMeter answering a location, the device keeps it, daylight
shows the sun's altitude, and the status names the source and its age.

**Acceptance Scenarios**:

1. **Given** source `sqmeter` and SQMeter's address set, **When** SQMeter answers a location,
   **Then** RoofRoof saves it (once) and daylight uses it.
2. **Given** a kept location, **When** SQMeter stops answering, **Then** the kept location stays,
   daylight carries on, and the status says SQMeter isn't answering and how old the location is.
3. **Given** no kept location, **When** SQMeter can't be read, **Then** daylight shows "Location not
   set", as today.
4. **Given** a GPS fix that changes only in its fourth decimal, **When** it is read again, **Then**
   nothing is saved.
5. **Given** source `manual`, **Then** RoofRoof never reads SQMeter's location and never changes the
   typed one.
6. **Given** the roof moving or a close waiting, **Then** no location read starts and no location
   save happens until it is still.

---

### User Story 2 - Upgrading changes nothing for a typed-in location (Priority: P1)

An owner who typed the location in under spec 013 upgrades. Their location and daylight behave as
before; Settings shows the source as "Typed in".

**Why this priority**: The constitution requires an upgrade to load old settings unchanged.

**Independent Test**: Settings stored by the previous release with a location load as `manual` with
the same values; without a location they load as `sqmeter`.

**Acceptance Scenarios**:

1. **Given** stored settings with `location.set` true, **When** they load, **Then** the source is
   `manual` and the latitude and longitude are unchanged.
2. **Given** stored settings with no location (or `set` false), **When** they load, **Then** the
   source is `sqmeter`.

---

### User Story 3 - Copy site settings from SQMeter (Priority: P2)

In Settings, the owner presses "Copy from SQMeter". RoofRoof lists what SQMeter has that differs:
time zone, NTP servers, MQTT broker, the weather topic, ntfy. Each row shows both values. The owner
ticks the time zone and the broker, applies, and saves. The list said to enter the MQTT password,
which SQMeter doesn't share.

**Why this priority**: It saves typing and typos, but the owner can always type the values.

**Independent Test**: Against a simulated SQMeter, the list shows the differing rows and the
secrets to enter; applying the ticked rows changes only those settings in the form.

**Acceptance Scenarios**:

1. **Given** SQMeter reachable, **When** the owner presses Copy, **Then** each copyable setting that
   differs is listed with both values, and those that match are not.
2. **Given** ticked rows, **When** the owner applies, **Then** only those settings change on the page
   and nothing is saved until Save.
3. **Given** SQMeter has an MQTT password, **Then** the broker row says to enter the password, and no
   password is copied.
4. **Given** a copied broker different from RoofRoof's, **When** saved, **Then** RoofRoof's old MQTT
   password is cleared.
5. **Given** SQMeter not answering, **Then** the page says so and nothing changes.
6. **Given** the roof moving, **When** Copy is pressed, **Then** it is refused with the reason.

---

### User Story 4 - From the wizard (Priority: P2)

On the Site and time page, the wizard first asks for SQMeter's address and offers "Copy site
settings from SQMeter". The later screens arrive filled in: the location follows SQMeter, the time
zone is SQMeter's, the weather address is set, the ntfy channel has its server and topic.

**Independent Test**: In the demo wizard, enter the address, copy, apply; the Time screen shows
SQMeter's time zone and the Weather address screen shows the address.

---

### User Story 6 - Settings keep following SQMeter (Priority: P2)

The owner moves the MQTT broker and changes the time zone on SQMeter. Within ten minutes RoofRoof
has the same broker and time zone, logs it, and asks for the new broker's password. In Settings
the followed fields say "Follows SQMeter · checked 3 min ago"; "Edit here instead" makes a group
local again.

**Independent Test**: With a simulated SQMeter whose settings change, the followed groups change
once, the local ones don't, an invalid value is refused with the old one kept, and the status
names the secret to enter.

**Acceptance Scenarios**:

1. **Given** the time group follows, **When** SQMeter's time zone changes, **Then** RoofRoof saves it
   at the next read and NTP uses it.
2. **Given** the MQTT group follows, **When** SQMeter's broker changes, **Then** RoofRoof saves the new
   broker, clears the old password and asks for the new one.
3. **Given** SQMeter sends a broker name that isn't valid, **Then** the old broker stays and the
   group's status says why.
4. **Given** a group is local, **Then** nothing in it ever changes from SQMeter.
5. **Given** the roof moving, **Then** no read and no sync save happens until it is still.
6. **Given** MQTT roof commands on, **Then** the broker isn't synced and the status says so.

---

### User Story 5 - In the demo (Priority: P3)

A visitor sees the demo's location come from a simulated SQMeter GPS, and copies the simulated
SQMeter's site settings.

**Independent Test**: The demo core answers `/api/sqmeter/site` and fills the status's location
from its simulated SQMeter; the demo panel's "SQMeter offline" stops both.

### Edge Cases

- SQMeter's address has a path other than `/api/safety` (a proxy): the scheme, host and port are
  used, with SQMeter's own paths.
- SQMeter reports `locationSource` `none`: the kept location stays; the status says SQMeter has no
  location.
- SQMeter answers a latitude or longitude out of range, or not a number: the read counts as
  unreadable and nothing is saved.
- An SQMeter older than the `sky` section (before v0.2): no location; the status says so.
- An SQMeter older than masking (v0.1): any non-empty secret is still reported only as "set".
- The address changes while a read runs: its answer is dropped.
- The clock isn't set when a location is saved: it is saved with no time, and the status shows its
  age only from reads in this run.
- The person switches to `manual` and back: the kept value stays in place until SQMeter answers.
- A copy fetched with an address typed in the wizard but not saved: the address is used for that
  fetch only.
- SQMeter's settings document is large (custom alert templates): only the parts the copy needs are
  parsed; the rest is skipped as it arrives.

## Requirements *(mandatory)*

### Functional Requirements

**Location from SQMeter**

- **FR-001**: The settings MUST gain `location.source`, `sqmeter` or `manual`.
- **FR-002**: With `sqmeter` and SQMeter's address set (`safety.httpUrl`, whether or not the weather
  source is on), the device MUST read SQMeter's `GET /api/status` from the address's scheme, host
  and port, and take `sky.latitude`, `sky.longitude` and `sky.locationSource`.
- **FR-003**: A read MUST count as good only with a 200 reply, `locationSource` `manual` or `gps`,
  and a finite latitude within ±90° and longitude within ±180°. Otherwise it is "not answering",
  "unreadable" or "no location".
- **FR-004**: Reads MUST run on the safety poller's task: soon after a start or a change of the
  address or the source, then every 10 minutes after a good read and every 2 minutes after a failed
  one; never in a poller pass that polls SQMeter's safety; each bounded to 3 s.
- **FR-005**: No read MUST start while the roof moves, a close waits or the automatic close is
  working.
- **FR-006**: Settings without `location.source` MUST load as `manual` when `location.set` is true
  and `sqmeter` otherwise; the latitude and longitude MUST load unchanged. A new device MUST start
  with `sqmeter`.
- **FR-007**: A good read MUST be saved to `location.latitude`, `location.longitude`, `location.set`
  (true), `location.sqmeter.gps` and `location.sqmeter.savedAt` (Unix seconds, 0 when the clock
  isn't set) only when no location is set, the GPS flag differs, or either coordinate differs by
  0.001° or more; through the flash-write guard, and not while the roof moves or a close waits.
- **FR-008**: While the source is `sqmeter` and a location is kept, a settings save from the API that
  keeps it `sqmeter` MUST keep the device's location fields (with none kept, the sent one stands,
  so a restored backup brings its location); `location.sqmeter.*` are never taken from the API.
- **FR-009**: SQMeter's location MUST be used only as the daylight failsafe's location. It MUST
  never change the weather verdict, the park check, or any other gate.
- **FR-010**: `GET /api/status` (and `/ws/status`) MUST carry a `location` section: the source,
  whether a location is set, whether it came from SQMeter's GPS, when it was saved, the age of the
  last good read in this run, and the last read's problem (`noAddress`, `waiting`, `unreachable`,
  `unreadable`, `noLocation`, or none).
- **FR-011**: SQMeter's location data MUST never be shown as a safety verdict.

**Copy from SQMeter**

- **FR-012**: `POST /api/sqmeter/site` MUST require the password when protection is on, take an
  optional `url` (an `http://` address; else `safety.httpUrl`), fetch SQMeter's `GET /api/config`
  on a short-lived task bounded to 5 s, and answer with the site document; one at a time.
- **FR-013**: It MUST refuse with the reason when no address is set or it is not `http://` (400),
  while the roof moves, a close waits or the automatic close works, or another copy runs (409), and
  when SQMeter can't be reached or read (502).
- **FR-014**: The site document MUST carry only: SQMeter's device name (for
  display), its location, time zone, NTP servers, MQTT broker, port, user name and base topic, ntfy
  server and topic, and for each secret (MQTT password, ntfy token, Pushover user key and app token)
  only a "set" flag. A secret's value MUST never appear in it, masked or not.
- **FR-015**: The copy MUST never offer the device name, host name, Wi-Fi, a password, token or key,
  Alpaca, OTA, or the MQTT and alert channel on/off switches.
- **FR-016**: The page MUST list each copyable setting that differs, with SQMeter's and RoofRoof's
  values and a tick box; apply only the ticked rows to the page's settings; and save only through
  the normal settings save.
- **FR-017**: Applying a different MQTT broker, port or user name MUST clear RoofRoof's saved MQTT
  password; applying a different ntfy server MUST clear the saved ntfy token. Each secret SQMeter
  has set, and each cleared one, MUST be flagged for the person to enter.
- **FR-018**: An older SQMeter that lacks a part MUST leave the part out (not an error); a v0.1
  top-level `timezone` MUST be read as the time zone.

**Where it shows**

- **FR-019**: Settings MUST show the location source (From SQMeter / Typed in); with SQMeter, the
  kept latitude and longitude read-only with where they came from and their age; and a "Copy from
  SQMeter" card with the review list.
- **FR-020**: The dashboard's daylight row MUST say where the location came from and how old it is,
  and a problem reading it.
- **FR-021**: The wizard's Site and time page MUST start with an SQMeter screen: its address and
  "Copy site settings from SQMeter". The wizard's page count and `setup.step` MUST NOT change, and
  it MUST offer no roof command.
- **FR-022**: The user guide (daylight, settings, wizard), the REST and WebSocket references, the
  configuration reference and the OpenAPI document MUST describe both features.
- **FR-023**: The demo MUST simulate SQMeter's location (a GPS fix) and settings, answer the copy
  route, and its "SQMeter offline" injector MUST make both fail.
- **FR-024**: The parsing of SQMeter's documents, the read schedule, the save rule, the migration and
  the site document MUST live in `lib/` with native tests.

**Keeping in step**

- **FR-025**: The settings MUST gain `sqmeterSync.time`, `.mqtt` and `.alerts`, each with `follow` and
  a device-owned `syncedAt`; a new device follows in every group; settings without the section load
  each group as local when its values differ from the defaults, else following.
- **FR-026**: With any group following and SQMeter's address set, the device MUST re-read SQMeter's
  `GET /api/config` through the copy's filter on the location's schedule rules (FR-004, FR-005), at
  most one SQMeter read per poller pass.
- **FR-027**: Each followed group MUST take SQMeter's values only through the validation a UI save
  meets; a group that fails MUST keep its old values and report why; a changed broker, port or user
  name MUST clear the saved MQTT password and a changed ntfy server the saved ntfy token; the MQTT
  group MUST NOT sync while MQTT roof commands are on.
- **FR-028**: A sync MUST save under the settings save lock and the flash-write guard, apply like
  any save, and log the groups that changed; matching values MUST write nothing after the group's
  first confirmation. A settings save MUST keep the live values of a group that follows in both the
  live and the new settings once the device has synced it.
- **FR-029**: Nothing synced MUST touch motion, the weather verdict, the park, Alpaca, WiFi, the device
  or host name, RoofRoof's own MQTT topic, any on/off switch, or any secret's value.
- **FR-030**: The status MUST carry a `sync` section: the last read's problem and age, and per group
  whether it follows, when it was synced, why its last sync was refused, and the secrets it needs.
- **FR-031**: Settings and the wizard MUST show a followed group's fields read-only with "Follows
  SQMeter" and the read's age, an "Edit here instead" control that unfollows it, a way to follow
  again, and a note asking for each needed secret; the dashboard's Status card a non-critical note
  per needed secret. The copy MUST switch the copied groups to follow.

### Key Entities

- **Location setting**: source (`sqmeter`/`manual`), set, latitude, longitude, and the device-owned
  `sqmeter.gps` and `sqmeter.savedAt`.
- **Location read**: the outcome of one read of SQMeter's status (good with a location and the GPS
  flag, or a problem) and when it was taken.
- **Site document**: what the copy can use from SQMeter's settings, with secrets reduced to "set".
- **Copy row**: one setting group SQMeter can fill, with both values, the secrets it needs, and
  whether it is ticked.
- **Sync group**: time, MQTT or alerts: whether it follows, when the device last changed it from
  SQMeter, the last refusal, and the secrets it needs.

## Design *(mandatory for UI changes)*

- DS rules: the Settings controls (`Field`, `SelectInput`, `SettingsCard`, `Toggle`, `Note`), the
  onboarding set in the wizard (DS-11 to DS-13, DS-29), copy per DS-20 to DS-28.
- The review list is a plain list of `Toggle` rows (label: the setting; detail: SQMeter's value ←
  RoofRoof's value) with secret notes, an "Apply ticked" button and a Cancel; no new styles.
- The dashboard's daylight row gains a second line in its value area; no new card.
- Mockups are to be reviewed by the owner with spec 018's redesign (PR #20), which this spec stacks
  on.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With SQMeter reachable, a new device with SQMeter's address shows the sun's altitude
  within 30 s of joining the network, with no location typed.
- **SC-002**: With SQMeter unreachable for a day, daylight keeps the last location and the status
  shows its age.
- **SC-003**: A device left reading a jittering GPS for a week makes no more than one location save
  per real move of 110 m or more.
- **SC-004**: Stored settings from the previous release load with the same daylight behaviour for
  every owner who typed a location (native migration test over the release fixtures).
- **SC-005**: No response of `/api/sqmeter/site` contains any secret's value (native test with an
  unmasked older-SQMeter document).
- **SC-006**: The poller's longest pass, a read included, stays within its pre-spec bound (6 s for
  SQMeter plus N.I.N.A.'s own), shown by the schedule's native tests.
- **SC-007**: Copying from SQMeter in the wizard fills the Time, Weather and Alerts screens in one
  step (Playwright in the demo).
- **SC-008**: A change on SQMeter reaches a following group within 10 minutes and 15 s, with one
  save; a week of unchanged settings writes nothing after the first confirmation.
- **SC-009**: SQMeter's settings with 20 alert templates of 480 characters (over 10 KB) parse into
  the filtered document without overflow (native test); the peak heap of a read is logged and
  checked on the bench.

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **Read from SQMeter's status, not its settings.** The status has the location SQMeter actually
   uses, including a GPS fix; the settings have only the typed one.
2. **Every 10 minutes after a good read, every 2 after a failed one, 3 s per read**, on the
   existing poller, never in a pass with SQMeter's safety poll, and not while the roof is busy. A
   site doesn't move; daylight changes by a fraction of a degree in ten minutes of a moving fix.
3. **Kept in the settings, saved only on a change of 0.001° or more**, through the flash-write
   guard. The settings are where daylight already looks, and a backup carries the location.
4. **Upgrades: a typed location stays typed (`manual`); no location becomes `sqmeter`.** For an
   owner with no location, daylight did nothing; once SQMeter answers it starts working with the
   mode the owner already chose. With "alert and close" that can now close the roof in daylight,
   which is the protective direction and still goes through spec 005's park check; it never opens.
5. **No alert when SQMeter's location can't be read.** It is not a safety input; the status and the
   dashboard show the problem and the age. SQMeter's weather source has its own alerts.
6. **The device owns the location while it follows SQMeter**: a save from an older page can't put
   an older value back.
7. **The copy is fetched by the device** (SQMeter sends no CORS headers), with the password, on a
   short-lived task, one at a time, refused while the roof is busy.
8. **No secret is ever copied or forwarded.** A copied MQTT broker, port or user name clears the
   saved MQTT password, and a copied ntfy server clears the saved ntfy token, so a saved secret is
   never sent to a different server.
9. **The on/off switches are not copied**: MQTT and each alert channel are turned on by the person,
   on their own screen, where their Test is.
10. **SQMeter's MQTT base topic becomes RoofRoof's MQTT weather topic** (`safety.mqttTopic`), which
    is where RoofRoof reads `<base>/safe`; RoofRoof's own base topic stays its own.
11. **Not offered: language** (RoofRoof is English only), **SQMeter's night threshold** (a
    different meaning), the device and host names, Wi-Fi, Alpaca, OTA, webhooks.
12. **The wizard asks for SQMeter's address first on the Site and time page** (a new screen, not a
    new page), so `setup.step` and resuming are unchanged.
13. **Three groups follow besides the location: time, MQTT (with SQMeter's base topic for the MQTT
    weather source) and the ntfy channel.** Pushover can't follow: SQMeter masks its user key.
14. **A new device follows everywhere; an upgrade keeps any group the person already changed
    local**, judged against the defaults (time zone and NTP servers; broker, port, user name and the
    weather topic; ntfy server and topic).
15. **Settings are re-read on the location's schedule with their own times** (one SQMeter read per
    poller pass, so the pass bound of assumption 2 holds); a change is saved once, matching values
    never write after the first confirmation.
16. **A value SQMeter sends that RoofRoof's validation refuses keeps the old value**, and that group's
    status says why; the other groups still sync.
17. **The device owns a followed group once it has synced it**; "Edit here instead" makes it local.
    A group never synced yet takes what is sent, so a copy in the wizard stands.
18. **No sync while MQTT roof commands are on** for the MQTT group: a broker from elsewhere must not
    change who may move the roof without a person.
19. **A missing secret is a note, not an alert**: the status names it, and Settings and the Status card
    ask for it. A synced server change clears the matching saved secret, so it is never sent to a
    server it wasn't saved for.
20. **A synced weather topic or broker restarts the MQTT weather source** as any settings change does
    (unknown, so not safe, until SQMeter's next message): the fail-safe direction. The verdict itself
    is never written by the sync.
