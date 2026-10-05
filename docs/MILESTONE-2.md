# Milestone 2: alert logic on native_sim

Goal: everything between a decoded header and the buzzer, plus the health checks, proven by scenario tests that run days of simulated time in seconds. Still no hardware.

Done when every test below passes in CI, a simulated week of standby runs in under a minute of wall time, and `docs/firmware-spec.md` is updated wherever a behaviour had to be pinned down.

## Tasks

1. **Watchdog interface.** Add `hal/watchdog.h` (start, feed, reset reason) as the tenth interface, with a fake that records feeds and, when starved, triggers a simulated reset that restarts the application.
2. **Event table.** `services/match/event_table.c`: every NWS SAME event code with display name and class (warning, watch, advisory, test, statement). Source it from the NWS NOAA Weather Radio event code list, and cite the page and date in the file header. Unknown codes are logged, never alerted.
3. **Settings model.** In-memory settings backed by the `storage` fake: home counties (up to 16), travel counties, mode (home or travel), weather channel (auto or 1–7), filter preset or custom bitmap. Bluetooth writes come in milestone 3; tests set values directly.
4. **Matcher.** A location matches a configured code when:
   - state and county match, and either side's subdivision digit is 0, or both subdivisions are equal;
   - the alert's county is 000 (whole state) and the state matches;
   - the alert's state is 00 (all of the U.S.).

   In travel mode the travel list replaces the home list; an empty travel list accepts every location.
5. **Filter.** Presets: warnings only, warnings and watches (default), all. RWT and RMT never alert; they are logged and feed the health check.
6. **Duplicates.** Key: originator, event, sorted locations, issue time, station. Suppress until issue time plus purge time. If the clock is unset, use receive time plus purge time. Handle the year boundary: a Julian day 365 or 366 header received on January 1 belongs to the previous year.
7. **Alert state machine.** The five states and transitions in the spec's alert-state table, driven by events (header, EOM, key, headphones, purge expiry, timers):
   - buzzer and vibration for 2 minutes, then a 3-second reminder every 5 minutes until a key or the purge time;
   - distinct vibration patterns for warnings and watches (extend `hal_alert_pattern`);
   - headphones in: buzzer off, amp on, broadcast plays; NNNN or headphones out goes to Silenced;
   - key lock: only the silence press works during an alert, and standby can't be turned off while locked;
   - Listening: alerts paused, automatic return to standby after 60 minutes without input.
8. **Screen model.** A `ui_model` struct naming the current screen and its fields (alert event, counties matched, expiry, warning reason, battery). Tests assert on it; rendering pixels is a later milestone.
9. **Health supervisor.** Implements the spec's health table: watchdog feeding only when audio blocks, decoder heartbeat and tuner status are all current; tuner reset after 3 consecutive faults; NO SIGNAL after 10 minutes below threshold, chirping hourly; no-weekly-test warning after 8 days; battery warnings at 20% and 5%, final beep and ship mode at 3.3 V.
10. **Clock correction.** Only RWT headers adjust the clock, and only when it is unset or more than 5 minutes from the RWT's issue time.
11. **Scenario runner.** A test helper that plays a timeline on `native_sim` with the fast clock: WAV audio at given times, signal-level changes, a battery curve, key presses, headphone plug and unplug. Assertions read the `alert_out` log, the `ui_model`, tuner calls and watchdog events. Build vectors with `tools/samegen/`.

## Tests

Matching and filtering:

- [x] Exact county match; subdivision 0 on either side matches; differing non-zero subdivisions don't
- [x] County 000 matches any configured county in that state; state 00 matches everything
- [x] Travel mode uses only the travel list; an empty travel list accepts all locations
- [x] Each filter preset passes and blocks the right classes; RWT and RMT never alert but are logged
- [x] Unknown event codes are logged, never alerted

Duplicates and time:

- [x] A re-broadcast inside the purge window: one alert, one log entry
- [x] The same header after purge expiry alerts again
- [x] A Julian day 366 header received on January 1 is placed in the previous year
- [x] Duplicate suppression works with the clock unset
- [x] RWT sets an unset clock; corrects a 10-minute error; leaves a 2-minute error alone
- [x] A re-broadcast TOR with an old issue time never changes the clock

Alert states:

- [x] Buzzer for 2 minutes, then reminders every 5 minutes, ending at a key press
- [x] Unanswered reminders end at the purge time; the radio returns to standby
- [x] Warning and watch use different vibration patterns
- [x] Headphones in during an alert: buzzer off, amp on; NNNN ends audio and goes to Silenced
- [x] Headphones out during alert audio goes to Silenced
- [x] Key lock during an alert: only silence works; standby can't be turned off while locked
- [x] Listening ignores alerts, shows alerts paused, and returns to standby after 60 minutes idle

Health:

- [x] Injected decoder stall: watchdog starves, simulated reset, standby resumes and decodes the next header
- [x] 3 tuner faults trigger a tuner reset; persistent faults end in a watchdog reset
- [x] Signal below threshold for 10 minutes: NO SIGNAL and hourly chirps; recovery clears it
- [x] 8 days without an RWT: warning; the next RWT clears it
- [x] A week of standby with a scripted discharge: warnings at 20% and 5%, final beep and ship mode at 3.3 V
- [x] That week runs in under a minute of wall time

Where the tests live: matching, filtering, time and duplicates in `tests/match/rules`; alert states, clock correction and the alert log in `tests/app/alert_manager`; health in `tests/app/health`; and the same behaviours end to end, with decoded audio and the week-long battery run, in `tests/scenario` (`tools/vectors` group `scenario`).

## Out of scope

Bluetooth (milestone 3), display rendering, real drivers.
