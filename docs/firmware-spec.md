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
- **Hardware interfaces:** tuner, audio_in, audio_out, alert_out, display, input, battery, clock, storage.
- **Drivers, chosen at build time:** board drivers for the nRF52840 (Si4743, SAADC, Sharp LCD over SPI, PAM8904E, MAX17048, BQ25180, GPIO) or `native_sim` fakes (WAV-file tuner audio, SDL display, scripted battery and signal, fast clock).

Application and service code never touches hardware directly; the interface layer is the seam where real drivers or laptop fakes plug in.

## Hardware interfaces

Nine interfaces separate the application from the hardware; each has a real driver for the board and a fake for `native_sim`. Application code includes only the interface headers, and the board or simulator is chosen at build time.

| Interface | Responsibilities | Real driver | Fake for native_sim |
| --- | --- | --- | --- |
| `tuner` | Power, band and frequency, seek, signal strength and SNR, mute, RDS text | Si4743 over I2C | Scripted signal-strength timeline; records every tune and mute call |
| `audio_in` | Continuous mono audio in fixed blocks for the decoder | SAADC with EasyDMA, timer-triggered, 4× gain, differential | Streams WAV files: RTL-SDR recordings and synthetic SAME bursts |
| `audio_out` | Headphone amp on and off, alert audio routing | TPA6132A2 enable GPIO | Logs state changes |
| `alert_out` | Buzzer pattern, vibration pattern, LED | PAM8904E enable, MOSFET GPIO, LED GPIO | Timestamped event log |
| `display` | 144×168 1-bit framebuffer, flush, backlight | Sharp memory LCD over SPI, polarity-toggle timer | Desktop window at real resolution (Zephyr SDL display driver) |
| `input` | Press, long press and combo events; lock switch; headphone detect | GPIO with debounce | Keyboard in the SDL window, or a scripted event list |
| `battery` | State of charge, voltage, charge status, temperature fault | MAX17048 and BQ25180 over I2C | Scripted discharge and charge curves |
| `clock` | UTC time, set and adjust, timers | nRF52 RTC on the 32.768 kHz crystal | Simulated time that can run faster than real time |
| `storage` | Settings and alert log as key-value records | Zephyr settings on flash | Settings in a local file |

Audio is sampled at 10,416.67 Hz (16 MHz ÷ 1,536), exactly 20 samples per SAME bit at 520.83 baud, which keeps bit timing integer. Bluetooth is not behind an interface: Zephyr's Bluetooth host runs unchanged on the `nrf52_bsim` simulator.

## Bluetooth settings service

One custom service carries all settings and status, and every custom characteristic requires an encrypted, authenticated (bonded) link. Firmware updates use the standard MCUmgr SMP service, also bonded-only. Generate one random 128-bit base UUID with `uuidgen`, freeze it, and number characteristics from it.

| Characteristic | Access | Contents |
| --- | --- | --- |
| Counties | Read, write | Home county list: count, then up to 16 six-digit SAME location codes (PSSCCC) |
| Travel counties | Read, write | Same format; written by the app from the phone's location; used only in travel mode |
| Mode | Read, write | Home or travel; weather channel auto-scan or fixed channel 1–7 |
| Event filter | Read, write | Preset (warnings only, warnings and watches, all) or a custom bitmap over the event table |
| Event table | Read, write | Versioned list of event code, display name and class (warning, watch, advisory, test, statement) |
| Time | Write | UTC seconds plus a POSIX time-zone string such as `EST5EDT,M3.2.0,M11.1.0` |
| Presets | Read, write | Up to 8 stations: band and frequency |
| Status | Read, notify | Battery percent, estimated hours left, signal quality, channel, last weekly test (UTC), health flags, lock state |
| Alert log | Read, notify | Last 16 alerts: raw SAME header and received time |
| Control | Write, indicate | Test alert, clear log, factory reset (confirmed on the radio) |

Every value starts with a 1-byte schema version; fixed fields are little-endian, lists are length-prefixed. Lists longer than the default packet size rely on the larger packet size iOS negotiates, with long writes as the fallback.

Connection rules:

- Advertising is off by default, so the 32 MHz crystal and its harmonics stay off.
- Long-press BAND opens a 2-minute connect window that only bonded phones can use (filter accept list).
- BAND + STBY opens a 60-second pairing window: LE Secure Connections, passkey shown on the LCD, the radio acting as display-only.
- At most 2 bonds; pairing a third replaces the oldest after on-screen confirmation.
- On connect, the app writes Time, and in travel mode also Travel counties.

## SAME decoding and alert logic

An alert fires only after a header is decoded, voted across its copies, matched to your counties and passed by the event filter. The pipeline runs in its own thread at the highest application priority.

1. **Tone detection.** Sliding one-bit correlators at 2,083.3 Hz (mark) and 1,562.5 Hz (space) over 20-sample windows; the bit is the sign of the energy difference.
2. **Bit sync.** Lock on the 16-byte 0xAB preamble, then nudge timing at each mark-space transition.
3. **Framing.** 8-bit bytes, least significant bit first; collect until the header's closing dash, or NNNN for end of message. Keep only the low 7 bits of each character: 47 CFR 11.31 sends 7-bit ASCII with an eighth null bit that may be 0 or 1. A header copy also ends when the carrier drops or at 252 characters, the longest valid header.
4. **Voting.** Hold up to 3 copies of a header; majority-vote each byte position, and accept 2 matching copies if the third is lost. One copy alone is never accepted. Vote after the third copy, or once 5 seconds pass after the latest copy with no new copy arriving (initial value; tune with recordings). End of message needs only one NNNN copy (at least 3 of its 4 characters); copies within 5 seconds of each other are one event, and a pending header is voted first so events stay in broadcast order.
5. **Parsing.** ZCZC, originator, 3-letter event code, 1 to 31 location codes, purge time, issue time (Julian day, UTC) and station ID. Reject anything malformed: fixed field lengths, upper-case letters in the originator and event code, digits in location, purge and issue fields, Julian day 001–366, hour 00–23, minute 00–59, and a station ID of 8 printable characters other than '-'. Purge time needs only 4 digits with minutes 00–59; nonstandard increments are accepted so a warning is never dropped over them.
6. **Matching.** A location matches when its state and county match a configured code. Subdivision digit 0 means the whole county, and county 000 means the whole state. Travel mode with no travel counties accepts every location from the current transmitter.
7. **Filtering.** Event class comes from the event table. Default: warnings and watches alert; weekly and monthly tests (RWT, RMT) never alert but are logged for the health check; everything else is logged only.
8. **Duplicates.** Suppress repeats with the same originator, event, locations, issue time and station until the purge time expires.

Alert states:

| From | Event | To |
| --- | --- | --- |
| Listening (AM/FM, alerts paused) | STBY press, or 60 min without input | Standby |
| Standby (decoding weather audio) | Tune or band press | Listening |
| Standby | Matching header | Alerting (buzzer, vibration, LED) |
| Alerting | No key | Repeats every 5 min until a key |
| Alerting | Any key | Silenced (alert on screen, LED) |
| Alerting | Headphones in | Alert audio (broadcast in headphones) |
| Alert audio | NNNN or headphones out | Silenced |
| Silenced | Purge time passes | Standby |

A key press silences an alert but keeps it on screen; only its purge time returns the radio to standby.

Alert behaviour:

- An unanswered alert sounds the buzzer and vibration for 2 minutes, then a 3-second reminder every 5 minutes until a key press or the purge time.
- Plugging in headphones during an alert stops the buzzer and plays the broadcast; end of message (NNNN) ends the audio.
- The key lock blocks every button except the silence press during an alert.
- While you listen to AM or FM the single tuner can't monitor weather: the screen shows alerts paused, and the radio returns to standby after 60 minutes without input.
- Each issue time also corrects the clock to the minute between phone syncs.

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
| Battery empty | 3.3 V cell voltage | Final long beep, ALERTS OFF screen, charger ship mode |

The weekly-test check is the only end-to-end proof that antenna, tuner and decoder work together, so it is never disabled. Signal-quality thresholds come from bring-up measurements.

## Power modes and budget

Standby dominates the battery at about 22 mA, nearly all of it the tuner receiving weather audio. Figures are estimates from datasheet typicals until measured.

| Mode | What's on | Estimated current |
| --- | --- | --- |
| Standby | Tuner on weather (20 mA typical), audio sampling and decoding, display static | ~22 mA |
| Listening | Tuner on AM or FM (FM 26 mA typical), headphone amp | ~30 mA |
| Alerting | Standby plus buzzer and pulsed vibration | Measure at bring-up; short duration |
| Bluetooth session | Standby plus 32 MHz crystal and radio | +1–3 mA while connected |
| Off | Charger ship mode | Microamps |

At the 4,000 mAh label rating, standby lasts about 180 hours (7.5 days). Real cell capacity and the 3.3 V cutoff bring that to 5–6 days.

Rules that keep it there:

- The 32 MHz crystal runs only during Bluetooth sessions; otherwise the chip uses its internal oscillator.
- The headphone amp is on only when headphones are in and audio is playing.
- In standby the display redraws at most once a minute; the backlight runs 5 seconds per press or alert.

## Simulation and test plan

The same application builds for three targets: `native_sim` on a laptop with fakes, `nrf52_bsim` for Bluetooth, and the real board. Most firmware risk can be retired before parts arrive.

Test audio:

- **Recordings.** Capture the local weather channel with the RTL-SDR for a month, including at least 4 weekly tests.
- **Synthetic bursts.** Generate SAME headers in Python with frequency offset up to ±2%, added noise at several SNRs, a missing copy, corrupted bytes, multiple location codes (including 000 and subdivisions) and end-of-message markers.
- **Negative audio.** Voice-only weather broadcasts, FM music, silence, and the 1050 Hz attention tone alone. Target: zero false alerts across 24 hours of it.
- Track decode rate against SNR; set pass thresholds after the first recordings. SNR is AFSK power over white-noise power across the full band (0 to 5.2 kHz at 10,416.67 Hz sampling), so 0 dB is about 10 dB Eb/N0. The table lives in `docs/decoder-snr.md`.

Scenario tests on `native_sim`, run with accelerated time:

- [ ] A week of standby with a scripted discharge: warnings at 20% and 5%, shutdown at 3.3 V
- [ ] Signal drops for 10 minutes: NO SIGNAL appears and chirps hourly
- [ ] 8 days with no weekly test: warning appears
- [ ] Same alert re-broadcast: one alert, one log entry
- [ ] Alert, then headphones plugged in: buzzer stops, audio plays, ends on NNNN
- [ ] Key lock on during an alert: only silence works
- [ ] Injected decoder hang: watchdog resets, standby resumes

Bluetooth tests on `nrf52_bsim`:

- [ ] Pairing with passkey succeeds inside the window and fails outside it
- [ ] Unbonded writes are rejected; bonded writes are accepted
- [ ] County lists longer than one packet write correctly

The iPhone app is built against a macOS mock peripheral that implements the same characteristics, since the iOS simulator has no Bluetooth. Run the `native_sim` and `bsim` suites in GitHub Actions on every push. Then move to hardware in the loop: the XIAO with RTL-SDR audio into its ADC, and finally the real board. SAME audio is never transmitted over the air.

## Open questions

- [ ] Is the Si4743's 54–67 mVrms output clean enough at the ADC's 4× gain, or does it need an op-amp?
- [ ] Does the Raytac module include a 32.768 kHz crystal?
- [ ] What is the LCD's maximum supply voltage, against the 3.1 V rail?
- [ ] Is the nRF52's internal oscillator accurate enough for SAME bit timing without the crystal? Verify on the XIAO.
- [ ] Which signal-quality thresholds mean no signal? Set from bring-up measurements.
- [ ] How much current do the buzzer and vibration draw during alerts?
- [ ] Are 16 counties and 2 bonds the right limits?
- [ ] What does the initial event table contain? Derive it from the NWS SAME event code list.
- [ ] Check the decoder's 47 CFR 11.31 details (eighth null bit, purge-time increments, 1-second copy spacing) against the regulation text. They were written from memory because the regulation sites were unreachable from the development container.
