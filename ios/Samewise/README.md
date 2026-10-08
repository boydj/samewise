# WX Radio for iPhone

The radio's setup app: pair, choose counties, sync the clock, set the
channel, filter and presets, and see status and the alert log. The radio
does all the alerting, so the app needs no background modes or
notifications.

Everything that isn't a view lives in `swift/SamewiseKit` (tested with
`swift test`, on Linux too); this project is the SwiftUI shell, and talks
to the radio only through SamewiseKit's `RadioLink`.

## Build

```
brew install xcodegen
cd ios/Samewise && xcodegen      # writes Samewise.xcodeproj (not committed)
open Samewise.xcodeproj
```

Run `xcodegen` again after pulling changes to `project.yml` or adding files.

To install on your iPhone with a free Apple ID, create `Local.xcconfig`
here (it's git-ignored) with your team and a bundle ID of your own, then
regenerate:

```
DEVELOPMENT_TEAM = ABCDE12345
SAMEWISE_BUNDLE_ID = com.yourname.samewise
```

Your team ID is under Xcode, Settings, Accounts. Free provisioning expires
after 7 days: run the app from Xcode again to re-sign it.

## The fake radio

The simulator has no Bluetooth, so there the app always uses
`FakeRadioLink`: GattModel's `RadioState` (the same model the Mac mock
serves) plus the radio's connection policy. The "Simulated radio" button
presses the radio's keys and injects events. On a phone, launch with
`-fakeRadio` to use it too; `-fakeWindow none|connect|pairing` and
`-fakeBonded` set its starting state.

Against the real Bluetooth stack, use the Mac mock
(`tools/mock-peripheral`, `swift run MockPeripheral --open`). iOS doesn't
pair with a Mac, so the mock runs unencrypted; pairing and the stale-bond
guidance are tested against the radio.

## Tests

- `swift test` in `swift/SamewiseKit`: the link, the session and every
  settings round trip, time zones, counties, error text.
- `xcodebuild test -scheme Samewise` (CI's `ios` job): the app's unit tests
  and the UI tests, on the simulator with the fake radio.
