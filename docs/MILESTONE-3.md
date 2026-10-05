# Milestone 3: Bluetooth settings on nrf52_bsim

Goal: the Bluetooth settings service from the spec, proven on Zephyr's `nrf52_bsim` simulator, plus a macOS mock peripheral built from the same characteristics table so the iPhone app can start before hardware exists. Task 0 fixes two milestone 2 bugs first.

Done when every test below passes in CI, `docs/gatt.json` is generated from the firmware table and checked in CI, and the mock peripheral builds.

## Tasks

0. **Milestone 2 fixes, tests first.** Write each failing test, then fix:
   - **Future issue times.** Day 366 received in a year after a non-leap year currently maps two years ahead, so the alert never expires. An inferred issue time more than 10 minutes in the future is not trusted: expire at receive time plus purge time and log it.
   - **Duplicate eviction.** Every header enters the 32-slot duplicate store before matching, so a busy transmitter can evict your own alert and its re-broadcast alerts again. Store only matched alerts and weekly tests.
1. **Characteristics table.** `services/ble/gatt_table.h` is the single source of truth: one X-macro row per characteristic with name, UUID offset, properties, permissions, maximum length and schema version. A host tool generates `docs/gatt.json`; CI fails if the two disagree.
2. **Codec.** `services/ble/codec.c`, plain C99 with no Zephyr includes, encodes and decodes every value: 1-byte schema version first, little-endian fields, length-prefixed lists. Malformed input is rejected without touching settings.
3. **Time zones.** A small POSIX TZ parser (plain C99) turns strings such as `EST5EDT,M3.2.0,M11.1.0` into local time, including daylight-saving transitions.
4. **Service.** Every characteristic in the spec table, using Zephyr's LE Secure Connections permissions (`BT_GATT_PERM_*_LESC`). Writes are validated, applied all-or-nothing, persisted through `storage`, and trigger a Status notification. Writing Time sets the clock and time zone.
5. **Connection policy.**
   - Advertising off by default.
   - Long-press BAND opens a 2-minute connect window restricted to bonded phones by the filter accept list.
   - BAND + STBY opens a 60-second pairing window: LE Secure Connections, display-only, passkey on a `ui_model` screen.
   - At most 2 bonds; a third replaces the oldest after on-screen confirmation.
   - Request the 32 MHz crystal from the power manager when a window opens; release it on close or disconnect.
6. **Control.** Test alert (runs the alert patterns, logged as a test), clear log, and factory reset, which waits for confirmation on the radio.
7. **Firmware updates.** Enable the MCUmgr SMP service with LE Secure Connections permissions. Image upload and swap are tested on hardware later; here only prove that unbonded access is refused.
8. **bsim tests.** Two-device tests (the radio plus a test central acting like the app) under `tests/bsim/`. The central reads the passkey through a test hook on the radio's `ui_model`. Add the BabbleSim modules to the `west.yml` allowlist, build BabbleSim in CI and cache it. Confirm the board name for the pinned Zephyr (`nrf52_bsim`).
9. **macOS mock peripheral.** `tools/mock-peripheral/`, a Swift package using CoreBluetooth's `CBPeripheralManager`, built from `docs/gatt.json`. It validates and stores writes, notifies Status, and can inject a fake alert from the keyboard. Characteristics require encryption, so iOS still pairs, but macOS controls the pairing method rather than a passkey on a screen; document that difference in its README.

## Tests

Milestone 2 fixes:

- [x] A day 366 header received in 2027 expires at receive time plus purge time, not in 2028
- [x] An issue time 15 minutes in the future is distrusted; 5 minutes in the future is accepted
- [x] 40 non-matching headers don't evict a matched alert; its re-broadcast stays suppressed
- [x] Weekly-test duplicates are still suppressed

Codec and time zones (host):

- [x] Every characteristic round-trips
- [x] Truncated values, oversized counts, wrong schema versions and trailing bytes are rejected, and settings are unchanged
- [x] County codes must be six digits with a valid subdivision digit
- [x] `EST5EDT,M3.2.0,M11.1.0` gives correct local time on both sides of both 2026 transitions
- [x] `UTC0` and `AEST-10AEDT,M10.1.0,M4.1.0/3` parse and convert correctly; malformed strings are rejected

Bluetooth on nrf52_bsim:

- [ ] No advertising by default
- [ ] Pairing fails outside the window; succeeds inside it with the right passkey; fails with a wrong one
- [ ] An unbonded central is refused on every custom characteristic and on SMP
- [ ] A bonded central reconnects inside the connect window and not outside it
- [ ] A third bond needs on-screen confirmation and replaces the oldest
- [ ] A 16-county list writes and reads back intact
- [ ] Status notifies on battery and signal changes; Alert log notifies on a new alert
- [ ] Writing Time sets the clock and local time
- [ ] An invalid write returns an ATT error and leaves settings unchanged
- [ ] Factory reset waits for on-device confirmation
- [ ] The 32 MHz crystal is requested only while a window is open or a phone is connected

Mock peripheral:

- [ ] Builds in CI on a macOS runner and serves every characteristic in `docs/gatt.json`

## Out of scope

The iPhone app's UI, firmware image swap on hardware, display rendering, real drivers.
