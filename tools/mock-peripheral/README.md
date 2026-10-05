# Mock peripheral

The radio's Bluetooth settings service on a Mac, so the iPhone app can be
built and tested before hardware exists (the iOS simulator has no
Bluetooth). It is generated from the same source as the firmware: it loads
`docs/gatt.json`, which `tools/gatt/gatt_json.py` builds from
`app/services/ble/gatt_table.h`, and starts with the firmware's default
event table from the same file.

```
swift run MockPeripheral                 # uses docs/gatt.json in this repository
swift run MockPeripheral path/to/gatt.json
swift test                               # the model; runs on Linux too
```

The first run asks for Bluetooth permission (System Settings, Privacy &
Security, Bluetooth).

## What it does

- Serves every characteristic in `docs/gatt.json` with the same layouts.
- Validates writes exactly as the radio does (a Swift port of
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

- **Pairing.** Every characteristic requires encryption, so iOS still pairs
  and bonds before it can read or write anything. But macOS, not this
  program, controls pairing: there is no passkey on a screen, no pairing or
  connect window, no two-bond limit, and advertising is always on. Test
  the passkey flow, the windows and bond replacement against the firmware
  (`tests/bsim/ble` runs them in simulation) or the hardware.
- **Factory reset** clears the settings and the log but can't remove the
  phone's bond; the radio unpairs every phone.
- Writing Time stores the clock and time zone but doesn't drive a clock.
- Mode doesn't tune anything; Status reports the chosen channel.
