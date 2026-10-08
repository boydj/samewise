# Mock peripheral

The radio's Bluetooth settings service on a Mac, so the iPhone app can be
built and tested before hardware exists (the iOS simulator has no
Bluetooth). It is generated from the same source as the firmware: it loads
`docs/gatt.json`, which `tools/gatt/gatt_json.py` builds from
`app/services/ble/gatt_table.h`, and starts with the firmware's default
event table from the same file.

The radio's behaviour lives in the shared Swift package `swift/GattModel`,
which the iPhone app uses too; this package adds only CoreBluetooth and the
keyboard.

```
swift run MockPeripheral --open          # for the iPhone app: no encryption (see Pairing below)
swift run MockPeripheral                 # encryption required, as on the radio
swift run MockPeripheral path/to/gatt.json
(cd ../../swift/GattModel && swift test) # the model; runs on Linux too
```

The first run asks for Bluetooth permission (System Settings, Privacy &
Security, Bluetooth).

## What it does

- Serves every characteristic in `docs/gatt.json` with the same layouts.
- Validates writes exactly as the radio does (GattModel's Swift port of
  `services/ble/codec.c` and the POSIX TZ parser) and returns the radio's
  ATT errors from `docs/gatt.json`; a rejected write changes nothing.
- Event table and alert log reads are indexed, and event table writes are
  staged (begin, entries, commit), as on the radio.
- Notifies Status on any accepted settings write, on battery or health
  changes, and when SNR or RSSI moves 3 dB; notifies the Alert log with
  each new entry; indicates Control results.

Keyboard commands (type `?` for the list):

| Command | Radio event |
| --- | --- |
| `a [EEE] [PSSCCC]` | A matching alert arrives (default `TOR 048453`): logged and notified |
| `t` | A test alert, as from the Control characteristic |
| `b <percent>` | Battery level |
| `s <snr> [rssi]` | Signal quality |
| `y` | Long-press STBY: confirm a pending factory reset |
| `n` | Any other key: cancel it |
| `p` | Print the settings |

## Differences from the radio

- **Pairing.** iOS doesn't pair with the mock. By default every
  characteristic requires encryption, as on the radio, but macOS answers an
  unencrypted request with Insufficient Encryption (ATT error 15), and iOS
  passes that to the app without starting pairing; the radio answers
  Insufficient Authentication (error 5), which makes iOS pair. So run the
  mock with `--open` for the app: nothing is encrypted and there is no
  bond. There is also no passkey, no pairing or connect window, no two-bond
  limit, and advertising is always on. Test the passkey flow, the windows,
  bond replacement and the stale-bond guidance against the firmware
  (`tests/bsim/ble` runs them in simulation) or the hardware.
- **Factory reset** clears the settings and the log but doesn't unpair or
  disconnect the phone; the radio unpairs every phone.
- Writing Time stores the clock and time zone but doesn't drive a clock.
- Mode doesn't tune anything; Status reports the chosen channel.
