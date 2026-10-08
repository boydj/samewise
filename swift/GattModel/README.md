# GattModel

The radio's Bluetooth settings service in Swift, shared by the macOS mock
peripheral (`tools/mock-peripheral`) and the iPhone app (`ios/Samewise`), so
there is one Swift port of the firmware's codec and POSIX TZ parser.

- `Codec.swift`: every characteristic's layout, a port of
  `app/services/ble/codec.c`.
- `PosixTimeZone.swift`: the TZ string check, a port of the firmware's parser.
- `RadioState.swift`: the radio's side of the service (validation, indexed
  reads, staged event table writes, notifications, Control), as the mock
  serves it and the app's fake link uses it.
- `GattDocument.swift`: `docs/gatt.json`. The package carries a copy as a
  resource (`GattDocument.bundled`); `tools/gatt/gatt_json.py` writes both
  and its `--check` fails if either is stale.

```
swift test        # Foundation only; runs on macOS and Linux
```

The tests check the model against `docs/gatt.json` in the repository.
