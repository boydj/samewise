# Pocket WX Radio — Firmware Architecture Spec

Exported Oct 4, 2026. Living version: https://claude.ai/code/artifact/58a9a1a9-ee83-45a4-9799-a0010c75ddac

## Scope and goals

The firmware's first job is never missing a matching SAME warning; AM/FM listening comes second. It runs on Zephyr (nRF Connect SDK) on the Raytac MDBT50Q-1MV2 (nRF52840), driving a Skyworks Si4743 tuner, Sharp LS013B7DH05 memory LCD, BQ25180 charger, MAX17048 fuel gauge, PAM8904E buzzer driver, coin vibration motor and TPA6132A2 headphone amp, all on one 3.1 V LDO rail.

The firmware must:

- Decode SAME from the tuner's analog audio and alert on matching events, using the three header copies each broadcast sends.
- Fail loudly: any condition that stops alerts (hang, lost signal, flat battery) produces a visible and audible warning.
- Last about a week of alert standby on 4,000 mAh; 5–6 days is the realistic expectation.
- Accept settings only from a bonded phone.

Design principles:

1. Every hardware dependency sits behind an interface, so the whole application also builds for Zephyr's `native_sim` and runs on a laptop.
2. The alert path never waits on the display, Bluetooth or flash.
3. Configuration is data: event codes, filters and time-zone rules are tables the phone can update, not code.

## Architecture overview

Four layers, top to bottom:

- **Application:** alert manager (alert states, buzzer, vibration, screens), radio UI (tuning, presets, key lock, display), health supervisor (watchdog, signal, weekly test, battery), settings (counties, filters, time zone, presets).
- **Services:** SAME decoder (tones, bit sync, header voting), event matcher (counties, filters, duplicates), power manager (modes, clocks, amp and backlight), Bluetooth service (settings, status, pairing, updates).
- **Hardware interfaces:** tuner, audio_in, audio_out, alert_out, display, input, battery, clock, storage, watchdog.
- **Drivers, chosen at build time:** board drivers for the nRF52840 (Si4743, SAADC, Sharp LCD over SPI, PAM8904E, MAX17048, BQ25180, GPIO) or `native_sim` fakes (WAV-file tuner audio, SDL display, scripted battery and signal, fast clock).

Application and service code never touches hardware directly; the interface layer is the seam where real drivers or laptop fakes plug in.

## Hardware interfaces

Ten interfaces separate the application from the hardware; each has a real driver for the board and a fake for `native_sim`. Application code includes only the interface headers, and the board or simulator is chosen at build time.

| Interface | Responsibilities | Real driver | Fake for native_sim |
| --- | --- | --- | --- |
| `tuner` | Power, band and frequency, seek, signal strength and SNR, mute, RDS text | Si4743 over I2C (`drivers/tuner_si4743.c`, AN332 Rev. 1.2); needs a 32.768 kHz reference on RCLK | Scripted signal-strength timeline; records every tune and mute call |
| `audio_in` | Continuous mono audio in fixed blocks for the decoder | SAADC with EasyDMA, timer-triggered, 4× gain, differential | Streams WAV files: RTL-SDR recordings and synthetic SAME bursts |
| `audio_out` | Headphone amp on and off, alert audio routing | TPA6132A2 enable GPIO | Logs state changes |
| `alert_out` | Buzzer pattern, vibration pattern, LED | PAM8904E enable, MOSFET GPIO, LED GPIO | Timestamped event log |
| `display` | 144×168 1-bit framebuffer, flush, backlight | Sharp memory LCD over SPI through Zephyr's `sharp,ls0xx` driver, which also toggles VCOM (`drivers/display_zephyr.c`, changed rows only) | Framebuffer in RAM, flushes counted (`fakes/display_fake.c`); the SDL window through the same driver |
| `input` | Press, long press and combo events; lock switch; headphone detect | GPIO with debounce | Keyboard in the SDL window, or a scripted event list |
| `battery` | State of charge, voltage, charge status, temperature fault, time to empty | MAX17048 and BQ25180 over I2C on Zephyr's drivers, plus the BQ25180's TS status and shutdown mode by register (`drivers/battery_zephyr.c`) | Scripted discharge and charge curves |
| `clock` | UTC time, set and adjust, timers | nRF52 RTC on the 32.768 kHz crystal | Simulated time that can run faster than real time |
| `storage` | Settings and alert log as key-value records | Zephyr settings on flash | Settings in a local file |
| `watchdog` | Start, feed, reset reason | nRF52 hardware watchdog | Records feeds; a starved watchdog triggers a simulated reset that restarts the application |

Audio is sampled at 10,416.67 Hz (16 MHz ÷ 1,536), exactly 20 samples per SAME bit at 520.83 baud, which keeps bit timing integer. Bluetooth is not behind an interface: Zephyr's Bluetooth host runs unchanged on the `nrf52_bsim` simulator.

## Bluetooth settings service

One custom service carries all settings and status, and every custom characteristic requires an encrypted, authenticated (bonded) link. Firmware updates use the standard MCUmgr SMP service, also bonded-only. Generate one random 128-bit base UUID with `uuidgen`, freeze it, and number characteristics from it.

| Characteristic | Access | Contents |
| --- | --- | --- |
| Counties | Read, write | Home county list: count, then up to 16 six-digit SAME location codes (PSSCCC) |
| Travel counties | Read, write | Same format; written by the app from the phone's location; used only in travel mode |
| Mode | Read, write | Home or travel; weather channel auto-scan or fixed channel 1–7 |
| Event filter | Read, write | Preset (warnings only, warnings and watches, all) or a custom bitmap over the event table |
| Event table | Read, write | Versioned list of event code, display name and class (warning, watch, advisory, test, statement), one entry per read; writes select an entry or replace the table (begin, entries, commit) |
| Time | Write | UTC seconds plus a POSIX time-zone string such as `EST5EDT,M3.2.0,M11.1.0` (up to 48 characters; a daylight-saving name without transition rules is rejected) |
| Presets | Read, write | Up to 8 stations: band and frequency |
| Status | Read, notify | Battery percent, estimated hours left, signal quality, channel, last weekly test (UTC), health flags, lock state |
| Alert log | Read, write, notify | Last 16 alerts: raw SAME header, received time, outcome and flags, one entry per read; a write selects the entry |
| Control | Write, indicate | Test alert, clear log, factory reset (confirmed on the radio) |

Every value starts with a 1-byte schema version; fixed fields are little-endian, lists are length-prefixed. Lists longer than the default packet size rely on the larger packet size iOS negotiates, with long writes as the fallback. The event table and alert log exceed the 512-byte attribute limit, so they are read one entry at a time. `services/ble/gatt_table.h` defines the UUIDs and maximum lengths, and `docs/gatt.json` is generated from it.

Connection rules:

- Advertising is off by default, so the 32 MHz crystal and its harmonics stay off.
- Long-press BAND opens a 2-minute connect window that only bonded phones can use (filter accept list).
- BAND + STBY opens a 60-second pairing window: LE Secure Connections, passkey shown on the LCD, the radio acting as display-only.
- At most 2 bonds; pairing a third replaces the least recently used phone after on-screen confirmation.
- On connect, the app writes Time, and in travel mode also Travel counties.
- Confirmations on the radio (third bond, factory reset): long-press STBY confirms; any other key, 30 seconds, or (for a reset) the phone disconnecting cancels.
- Keys open windows only from Standby or Listening with the lock off; a press that silences an alert does nothing else. One connection at a time.
- The 32 MHz crystal is requested while a window is open or a phone is connected.

Behaviour of the characteristics:

- Characteristics answer only an LE Secure Connections authenticated link.
- Writes are validated completely before anything changes; a rejected write returns an ATT error from `docs/gatt.json` and changes nothing. A storage failure returns its own error: the value is in effect until the next restart.
- Every accepted settings write notifies Status. Status also notifies when battery percent, health flags, lock or channel change, or SNR or RSSI moves 3 dB from the last notified value. Hours left is the MAX17048's time to empty at its measured discharge rate; until the gauge has a rate (just after boot) and while charging, it is charge × 132 hours (5.5 days of standby on a full cell). The screens show the same figure.
- Event table writes are staged: begin (version, count), entries in index order, commit. An empty table is refused, since it would silence every alert.
- Alert log notifies its newest entry when one is stored.
- Control: a test alert runs the warning patterns and the alert screen for 2 minutes, shows Practice/Demo Warning and is logged as a test; it is refused unless the radio is in Standby, so it can never mask a real alert. Results arrive as indications.

## SAME decoding and alert logic

An alert fires only after a header is decoded, voted across its copies, matched to your counties and passed by the event filter. The pipeline runs in its own thread at the highest application priority.

1. **Tone detection.** Sliding one-bit correlators at 2,083.3 Hz (mark) and 1,562.5 Hz (space) over 20-sample windows; the bit is the sign of the energy difference.
2. **Bit sync.** Lock on the 16-byte 0xAB preamble, then nudge timing at each mark-space transition.
3. **Framing.** 8-bit bytes, least significant bit first; collect until the header's closing dash, or NNNN for end of message. Keep only the low 7 bits of each character: 47 CFR 11.31 sends 7-bit ASCII with an eighth null bit that may be 0 or 1. A header copy also ends when the carrier drops or at 252 characters, the longest valid header.
4. **Voting.** Hold up to 3 copies of a header; majority-vote each byte position, and accept 2 matching copies if the third is lost. One copy alone is never accepted. Vote after the third copy, or once 5 seconds pass after the latest copy with no new copy arriving (initial value; tune with recordings). End of message needs only one NNNN copy (at least 3 of its 4 characters); copies within 5 seconds of each other are one event, and a pending header is voted first so events stay in broadcast order.
5. **Parsing.** ZCZC, originator, 3-letter event code, 1 to 31 location codes, purge time, issue time (Julian day, UTC) and station ID. Reject anything malformed: fixed field lengths, an originator of EAS, CIV, WXR or PEP (the only codes 47 CFR 11.31(d)(1) allows), 3 upper-case letters for the event code, digits in location, purge and issue fields, Julian day 001–366, hour 00–23, minute 00–59, and a station ID of 8 printable characters other than '-' and '+' (11.31(b) reserves both as separators). Event codes are not checked against a list here; the event table classifies them. Purge time needs only 4 digits with minutes 00–59: 11.31 specifies 15-minute steps up to an hour and 30-minute steps beyond, but nonstandard increments are accepted so a warning is never dropped over them.
6. **Matching.** A location matches when its state and county match a configured code. Subdivision digit 0 on either side means the whole county; otherwise the subdivisions must be equal. County 000 on either side means the whole state, so a configured 048000 matches every Texas county. State 00 in an alert means all of the U.S. An empty county list accepts every location: travel mode with no travel counties, and home mode before any home county is set, so an unconfigured radio still warns.
7. **Filtering.** Event class comes from the event table. Presets: warnings only; warnings and watches (default); all, meaning every class except tests; or a custom bitmap over the event table. Tests (RWT, RMT, NPT, DMO) never alert under any preset, even a custom one, but are logged, and RWT feeds the health check. Codes missing from the table are logged, never alerted. Urgent codes whose names don't say Warning (EAN, EVI, CAE, CDW, CEM, LAE, SPW) are classed as warnings.
8. **Duplicates.** Suppress repeats with the same originator, event, locations (in any order), issue time and station until the alert expires: issue time plus purge time, or receive time plus purge time while the clock is unset. Headers carry no year, so the year is the one that puts the issue time closest to now (a day 365 or 366 header received on January 1 belongs to the previous year). A header that has already expired when it arrives, with the clock set, is logged as expired and never alerts. An inferred issue time more than 10 minutes in the future is not trusted (a corrupted day 366, say): the alert expires at receive time plus purge time instead. Only matched alerts and weekly tests enter the duplicate store, so other counties' traffic on a busy day can't evict yours.

Alert states:

| From | Event | To |
| --- | --- | --- |
| Listening (AM/FM, alerts paused) | STBY press, or 60 min without input | Standby |
| Standby (decoding weather audio) | Tune or band press | Listening |
| Standby | Matching header | Alerting (buzzer, vibration, LED) |
| Alerting | No key | Repeats every 5 min until a key |
| Alerting | Any key | Silenced (alert on screen, LED) |
| Alerting | Headphones in | Alert audio (broadcast in headphones) |
| Alert audio | NNNN, headphones out or any key | Silenced |
| Silenced | Purge time passes | Standby |
| Alerting, Alert audio or Silenced | New matching alert (not a duplicate) | Alerting, or Alert audio if headphones are in |
| Standby | Matching header with headphones already in | Alert audio |

A key press silences an alert but keeps it on screen; only its purge time returns the radio to standby. With several alerts active the screen shows the newest, and the radio returns to standby when the last one expires. While Silenced, tune and band presses are ignored. While Listening the tuner isn't on weather, so decoded headers are ignored entirely.

Alert behaviour:

- An unanswered alert sounds the buzzer and vibration for 2 minutes, then a 3-second reminder every 5 minutes (the first 5 minutes after the 2-minute period ends) until a key press or the purge time.
- Plugging in headphones during an alert stops the buzzer and plays the broadcast; end of message (NNNN) ends the audio.
- The key lock blocks every button except the silence press during an alert. During an alert any key only silences, locked or not; outside an alert the lock ignores every key, so standby can't be turned off while locked. Ignored presses don't count as input for the 60-minute listening timeout.
- While you listen to AM or FM the single tuner can't monitor weather: the screen shows alerts paused, and the radio returns to standby after 60 minutes without input.
- Only weekly tests (RWT) correct the clock, and only when the clock is unset or more than 5 minutes from the RWT's issue time. Other headers never touch the clock: a re-broadcast alert can carry an issue time hours old. Headers carry no year, so setting an unset clock takes the first year at or after the last UTC the radio stored (or the firmware's epoch). That stored time is refreshed on every clock set and daily. An RWT never moves a set clock by more than a day: a larger jump means a wrongly inferred year (day 366 in a non-leap year), which would make every later alert look expired, so it is counted and ignored.
- The alert log keeps the last 16 non-duplicate headers that matched the counties, plus every test, each with its received time and outcome (alerted, filtered, unknown event, expired). Entries wait in RAM and are written to flash at low priority, so the alert path never waits on flash.
- Vibration uses distinct patterns for warnings and watches, so the class is felt without looking.

## Screens

The display is the Sharp memory LCD, 144×168 pixels, 1 bit. Every screen is drawn from the screen model (`app/ui_model.h`) by `app/screens.c` in VT323, the pixel font of the design (SIL Open Font License): 17 px for small text, 25 px for headings and 25 px at double scale for big digits. Renders of every screen are in `docs/images/screens/`, and `tests/display/screens` holds each one to a golden image.

The screen shown follows the model's precedence: ALERTS OFF, alert, Bluetooth, RESTARTED, warning, listening, standby.

| Screen | Shows |
| --- | --- |
| Standby | WX tag, BT when a phone is connected, battery time left and gauge; the weather frequency, MONITORING; county count (or ALL AREAS with no counties, SAME or TRAVEL), filter, last alert (event and time today, days ago before that, NONE) |
| Listening | Band, ST for FM stereo, signal bars, battery; frequency with MHz or kHz, RDS radio text, presets P1–P4 around the current one, volume |
| Alert | ALERT header (TEST for a test alert) with +N for more alerts active; the event name on up to two lines (three, smaller, for long names); UNTIL hh:mm local, or FOR n MIN while the clock is unset; counties matched; then PLUG IN TO LISTEN / ANY KEY SILENCES, PLAYING BROADCAST / ANY KEY STOPS, or SILENCED |
| Warning | The most urgent of battery critical, no signal, tuner fault, no weekly test and battery low, with +N for the others. No signal: WX frequency, ALERTS AT RISK, MOVE NEAR A WINDOW OR CHECK LANYARD. Tuner fault: RADIO CHIP NOT ANSWERING. No weekly test: NO WEEKLY TEST IN 8 DAYS. Battery: percent, about how long, ALERTS STOP AT 0%, PLUG IN USB-C |
| ALERTS OFF | BATTERY EMPTY, RADIO IS OFF, PLUG IN USB-C TO TURN BACK ON |
| RESTARTED | RECOVERED FROM A FAULT, ALERTS ARE ON, NOTHING TO DO |
| Bluetooth | Connect window (paired phones only), pairing window and passkey, each with seconds left; REPLACE PHONE? and FACTORY RESET? confirmations (HOLD STBY to confirm, any other key cancels) |

Text that doesn't fit is cut, never wrapped off the screen; nothing is drawn in the outer two columns except the header bar.

Signal bars: none below the no-signal SNR threshold, then one more for every 5 dB, up to four. Battery time left shows as days with a decimal from 48 hours up (6.2d), hours below (14h), CHG while charging.

Drawing never runs on the alert path. Once a second at low priority the UI step copies the screen model under the app lock, then draws and flushes without it. The panel changes at once when the screen changes or anything changes off standby; in standby at most once a minute. The backlight runs 5 seconds after each key press and each new alert.

## Health monitoring

A supervisor thread owns every check that could silently stop alerts, and only it feeds the hardware watchdog. The nRF52 watchdog runs during sleep and can't be stopped by firmware once started.

| Condition | Trigger | Response |
| --- | --- | --- |
| Firmware hang | Supervisor misses a heartbeat: audio blocks stop arriving, decoder thread stalls, or no valid tuner status for 10 s | Watchdog reset after 8 s; on boot, log the reset reason, show RESTARTED briefly, resume standby |
| Tuner fault | 3 consecutive I2C errors or invalid status | Reset and re-tune the tuner; if that fails, let the watchdog reset the radio |
| No signal | Signal quality below threshold for 10 minutes | NO SIGNAL screen, chirp every hour |
| No weekly test | No valid RWT decoded in 8 days | Same warning as no signal, with its own reason line |
| Battery low | 20% state of charge | Chirp and screen warning |
| Battery critical | 5% state of charge | Chirp every 30 minutes |
| Battery empty | 3.3 V cell voltage | Final long beep, ALERTS OFF screen, charger off: the BQ25180's shutdown mode, which only plugging in USB-C wakes (15 nA) |

The weekly-test check is the only end-to-end proof that antenna, tuner and decoder work together, so it is never disabled. Signal-quality thresholds come from bring-up measurements; until then the no-signal threshold is a configuration value (SNR below 10 dB).

How the checks run:

- The supervisor ticks once a second. It feeds the watchdog only while audio blocks and decoder progress are under 2 seconds old and a valid tuner status is under 10 seconds old. While Listening the tuner is on AM or FM and weather decoding stops, so only the tuner status counts. A stall therefore resets the radio within about 11 seconds.
- After a watchdog reset the count of such resets is kept in storage, RESTARTED shows for 10 seconds, and the radio resumes standby. The clock and the duplicate list are lost with RAM, as on the real board.
- A tuner fault (3 consecutive failed or invalid status reads) power-cycles the tuner and re-tunes the last weather frequency. If that doesn't help, the stale tuner status starves the watchdog.
- No signal is judged only on weather (not while Listening), and any recovery restarts its 10 minutes. No weekly test counts 8 days from boot or the last RWT. Either shows the warning screen with its own reason line and chirps at once, then hourly.
- Battery is read once a minute. A warning clears when charging or 5 points above its threshold.
- Chirps never sound over an Alerting or Alert audio state; the warning is still recorded, and the alert outranks it on screen.

## Power modes and budget

Standby dominates the battery at about 22 mA, nearly all of it the tuner receiving weather audio. Figures are estimates from datasheet typicals until measured.

| Mode | What's on | Estimated current |
| --- | --- | --- |
| Standby | Tuner on weather (20 mA typical), audio sampling and decoding, display static | ~22 mA |
| Listening | Tuner on AM or FM (FM 26 mA typical), headphone amp | ~30 mA |
| Alerting | Standby plus buzzer and pulsed vibration | Measure at bring-up; short duration |
| Bluetooth session | Standby plus 32 MHz crystal and radio | +1–3 mA while connected |
| Off | Charger shutdown mode | 15 nA |

At the 4,000 mAh label rating, standby lasts about 180 hours (7.5 days). Real cell capacity and the 3.3 V cutoff bring that to 5–6 days.

Rules that keep it there:

- The 32 MHz crystal runs only during Bluetooth sessions; otherwise the chip uses its internal oscillator.
- The headphone amp is on only when headphones are in and audio is playing.
- In standby the display redraws at most once a minute; the backlight runs 5 seconds per press or alert.

## Simulation and test plan

The same application builds for three targets: `native_sim` on a laptop with fakes, `nrf52_bsim` for Bluetooth, and the real board. Before the radio board exists, the XIAO nRF52840 image also runs in Renode's emulated nRF52840, decoding a SAME clip compiled into flash, to check that it fits and to estimate decoder CPU load (`docs/xiao-bench.md`). Most firmware risk can be retired before parts arrive.

Test audio:

- **Recordings.** Capture the local weather channel with the RTL-SDR for a month, including at least 4 weekly tests.
- **Synthetic bursts.** Generate SAME headers in Python with frequency offset up to ±2%, added noise at several SNRs, a missing copy, corrupted bytes, multiple location codes (including 000 and subdivisions) and end-of-message markers.
- **Negative audio.** Voice-only weather broadcasts, FM music, silence, the 1050 Hz NOAA Weather Radio alarm tone alone, and the EAS attention signal (853 and 960 Hz together, 11.31(a)(2)). Target: zero false alerts across 24 hours of it.
- Track decode rate against SNR; set pass thresholds after the first recordings. SNR is AFSK power over white-noise power across the full band (0 to 5.2 kHz at 10,416.67 Hz sampling), so 0 dB is about 10 dB Eb/N0. The table lives in `docs/decoder-snr.md`.

Scenario tests on `native_sim`, run with accelerated time (`tests/scenario`):

- [x] A week of standby with a scripted discharge: warnings at 20% and 5%, shutdown at 3.3 V (`test_week_of_standby_battery_runs_fast`)
- [x] Signal drops for 10 minutes: NO SIGNAL appears and chirps hourly (`test_signal_drop_no_signal_hourly_chirps_recovery`)
- [x] 8 days with no weekly test: warning appears (`test_8_days_without_rwt_then_rwt_clears_and_sets_clock`)
- [x] Same alert re-broadcast: one alert, one log entry (`test_rebroadcast_one_alert_one_log_entry`)
- [x] Alert, then headphones plugged in: buzzer stops, audio plays, ends on NNNN (`test_alert_then_headphones_audio_ends_on_nnnn`)
- [x] Key lock on during an alert: only silence works (`test_key_lock_during_alert`)
- [x] Injected decoder hang: watchdog resets, standby resumes (`test_decoder_stall_resets_and_next_header_decodes`)

Bluetooth tests on `nrf52_bsim`:

- [x] Pairing with passkey succeeds inside the window and fails outside it
- [x] Unbonded writes are rejected; bonded writes are accepted
- [x] County lists longer than one packet write correctly

The iPhone app is built against a macOS mock peripheral that implements the same characteristics, since the iOS simulator has no Bluetooth. Run the `native_sim` and `bsim` suites in GitHub Actions on every push. Then move to hardware in the loop: the XIAO with RTL-SDR audio into its ADC, and finally the real board. SAME audio is never transmitted over the air.

## Open questions

- [ ] Is the Si4743's 54–67 mVrms output clean enough at the ADC's 4× gain, or does it need an op-amp?
- [ ] Does the Raytac module include a 32.768 kHz crystal?
- [ ] What is the LCD's maximum supply voltage, against the 3.1 V rail?
- [ ] Where does the Si4743's 32.768 kHz RCLK come from? The Si474x has no crystal oscillator option (AN332, POWER_UP: XOSCEN must be 0), so the board must feed it, from the nRF52's 32.768 kHz crystal or its own oscillator.
- [ ] Is the nRF52's internal oscillator accurate enough for SAME bit timing without the crystal? Verify on the XIAO.
- [ ] Which signal-quality thresholds mean no signal? Set from bring-up measurements.
- [ ] How much current do the buzzer and vibration draw during alerts?
- [ ] Are 16 counties and 2 bonds the right limits?
- [x] What does the initial event table contain? The 53 operational codes on the NWS "NWR NWS Event Codes" page (weather.gov/nwr/eventcodes, printed Oct 4, 2026), plus EAN, NIC, NPT, MEP and NMN from 47 CFR 11.31 Table 2, which the NWS page doesn't list: 58 codes in `app/services/match/event_table_default.c`. Classes follow the FCC naming convention (W warning, A watch, S statement). TOR, SVR, EVI, EAN, CAE, CEM and LAE are warnings; BLU, TOE, ADR, NIC, NMN and MEP are advisories; RWT, RMT, NPT and DMO are tests.
- [x] Check the decoder's 47 CFR 11.31 details against the regulation text (eCFR, up to date as of Oct 1, 2026). Confirmed: 7-bit ASCII with an eighth null bit of 0 or 1, 520.83 bit/s with 1.92 ms bits, mark 2083.3 Hz and space 1562.5 Hz, 16 bytes of 0xAB before every header and EOM, one-second pauses between the 3 copies, up to 31 locations, P = 0 for a whole county, CCC 000 for a whole state and SS 00 for all of the U.S. Changed to match: only EAS, CIV, WXR and PEP originators, no '+' in the station ID, and the EAS two-tone attention signal added to negative audio.
