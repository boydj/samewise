# App milestone 1: iPhone setup app against the mock peripheral

Goal: a SwiftUI app that pairs with the radio, configures it, syncs time and shows status and the alert log, built and tested entirely against the macOS mock peripheral and an in-app fake. The radio does all alerting; the app is for setup and review, so it needs no background modes or notifications.

Done when CI builds the app and passes its unit and UI tests on a macOS runner, and the manual checklist passes on your iPhone against the Mac mock.

## Tasks

1. **Shared Swift codec.** Move `GattModel` out of `tools/mock-peripheral/` into a shared Swift package (`swift/GattModel/`) that both the mock and the app depend on, so there is one Swift port of the codec and TZ parser. Its existing tests keep checking it against `docs/gatt.json`.
2. **App project.** `ios/Samewise/`, SwiftUI, iOS 17 or later. It installs with a free Apple ID (re-sign every 7 days); no paid developer account is needed for this milestone.
3. **Radio link.** A `RadioLink` protocol with two implementations: CoreBluetooth for real use, and an in-memory fake backed by `GattModel` for unit tests, UI tests, SwiftUI previews and the simulator (which has no Bluetooth). The rest of the app sees only `RadioLink`.
4. **Connect and pair.** Scan for the service UUID. The radio only advertises in its windows, so the screen tells you what to press: hold BAND to reconnect, BAND + STBY to pair a new phone. Pairing uses the iOS system prompt on first encrypted access. Handle iOS's stale-bond error (the radio forgot the phone) by explaining how to forget the device in iOS Settings and pair again.
5. **Time sync on every connect.** Write UTC plus a POSIX TZ string derived from the phone's time zone: offsets from `TimeZone`, and daylight-saving rules from its next two transitions converted to `Mm.w.d/time` form. Zones whose rules can't be expressed that way get their current offset only, with a note that it refreshes on each connection.
6. **Counties.** Search by county and state name; the app converts to SAME codes (subdivision 0, state FIPS, county FIPS) from a bundled county table. Source it from the U.S. Census Bureau FIPS county list, including territories, and cite the source and date in the file. Advanced: pick a subdivision or a whole state.
7. **Travel mode.** A toggle, plus "use my location": CoreLocation with When In Use permission, reverse-geocode to county, map to a SAME code, and write Travel counties. The app refreshes them automatically on connect while travel mode is on.
8. **Other settings.** Weather channel (auto or 1–7), alert filter (presets, or a custom list built from the radio's event table), and up to 8 station presets.
9. **Status and alert log.** A home screen with battery, hours left, signal, channel, last weekly test, health warnings and lock state, live from Status notifications. An alert log with event names from the event table and times in local time.
10. **Control.** Test alert, clear log, and factory reset, which tells you to confirm on the radio (long-press STBY) and shows the result from the indication.
11. **Errors and accessibility.** Map every ATT error in `docs/gatt.json` to plain language; a rejected write leaves the screen showing the radio's actual values. Support Dynamic Type and VoiceOver labels throughout.

## Tests

Unit tests:

- [x] The county table converts names to SAME codes and back, including territories
- [x] POSIX TZ strings generated for at least 10 zones (U.S. zones, Arizona, Hawaii, a southern-hemisphere zone, Europe/London) match `TimeZone` offsets every 15 minutes across 2026–2028, checked through `GattModel`'s TZ parser
- [x] Every settings screen round-trips through the fake link
- [x] A rejected write shows the mapped error and the screen reverts to the radio's values
- [x] Status and Alert log notifications update the screens

UI tests on the simulator with the fake link:

- [x] First run: pair, choose counties, see them on the home screen
- [x] Travel mode with a mocked location writes the right travel counties
- [x] Factory reset waits for confirmation and shows the result

Manual on your iPhone with the Mac mock (`swift run MockPeripheral --open`):

- [ ] Connects; settings writes appear in the mock's state
- [ ] `a TOR <code>` in the mock shows up in the alert log; `b` and `s` update the home screen
- [ ] Test alert and factory reset (confirm with `y`, cancel with `n`) behave as on the radio

iOS doesn't pair with the Mac mock: with encryption required, macOS answers Insufficient Encryption (ATT error 15) and iOS reports it to the app without starting pairing (the radio answers Insufficient Authentication, which makes iOS pair). So the mock runs open, and these two checks wait for the XIAO in firmware milestone 4. Until then the radio's side is covered by `tests/bsim/ble` and the app's by the fake link's tests.

- [ ] Pairs through the iOS prompt with the radio's passkey
- [ ] Forgetting the radio's bond produces the stale-bond guidance, and re-pairing works

## Out of scope

Firmware updates from the app (later, with Nordic's iOS DFU library), App Store distribution, widgets.
