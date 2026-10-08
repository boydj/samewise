# Pocket WX Radio firmware

Pocket AM/FM/NOAA weather radio with SAME alerts: Raytac MDBT50Q-1MV2 (nRF52840) plus a Skyworks Si4743 tuner, on Zephyr via the nRF Connect SDK.

- Spec (source of truth): `docs/firmware-spec.md`. Living version: https://claude.ai/code/artifact/58a9a1a9-ee83-45a4-9799-a0010c75ddac
- Current task: `docs/MILESTONE-APP-1.md`, the iPhone app (firmware milestone 4 waits for the XIAO)

## Priorities

1. Never miss a matching SAME warning. The alert path never blocks on the display, Bluetooth or flash.
2. Fail loudly. Anything that can stop alerts must produce a warning (spec: Health monitoring).
3. Everything runs on `native_sim`. If something can't be tested on a laptop, restructure it until it can.

## Layout

```
app/
  src/main.c
  src/app/          alert manager, radio UI, health supervisor, settings
  services/same/    SAME decoder and parser (plain C99, no Zephyr includes)
  services/match/   event matcher, filter, duplicates (milestone 2)
  services/power/   power manager (milestone 2)
  services/ble/     Bluetooth settings service (milestone 3)
  hal/              the ten interface headers
  drivers/          real drivers (milestone 5)
  fakes/            native_sim fakes
  boards/           board overlays
tests/              ztest suites, run with twister on native_sim; tests/bsim on nrf52_bsim
swift/GattModel/    shared Swift codec, TZ parser and radio model (app and mock)
ios/Samewise/       iPhone setup app (SwiftUI)
tools/mock-peripheral/  macOS stand-in for the radio over Bluetooth
tools/samegen/      synthetic SAME generator (Python)
tools/vectors/      WAV test vectors (git LFS) with JSON sidecars
tools/counties/     the app's county table, built from the Census FIPS lists
docs/               spec, milestones, decoder reports
```

## Build and test

```
west build -b native_sim app -p auto
./build/app/zephyr/zephyr.exe --stop_at=1
west twister -T tests -p native_sim
python3 -m unittest discover -s tools -p 'test_*.py'
python3 tools/samegen/samegen.py --help
tests/bsim/run.sh    # Bluetooth on nrf52_bsim; needs BabbleSim, see below
west build -b xiao_ble/nrf52840 app -d build-xiao      # needs the Zephyr SDK's ARM toolchain
python3 tools/size/size_report.py build-xiao/app/zephyr/zephyr.elf --json build-xiao/size.json
tests/renode/run.sh <renode dir> build-xiao renode-out [native_sim console log]
(cd swift/GattModel && swift test)                     # also runs on Linux
(cd swift/SamewiseKit && swift test)                   # the app's logic; also runs on Linux
(cd tools/mock-peripheral && swift run MockPeripheral --open) # macOS; --open for the iPhone
(cd ios/Samewise && xcodegen)                          # macOS; then open Samewise.xcodeproj
```

Workspace setup (T2 layout, from the directory containing this repo): `west init -l samewise && west update --narrow -o=--depth=1`. Zephyr in the pinned NCS needs Python 3.12 or newer; install `zephyr/scripts/requirements-{base,build-test,run-test}.txt` and `tools/requirements.txt`. `native_sim` needs `gcc-multilib`, and without the Zephyr SDK set `ZEPHYR_TOOLCHAIN_VARIANT=host`. NCS builds use sysbuild by default, so the app binary lands in `build/app/`. Synthetic test vectors are generated into each test's build directory at build time (`tools/vectors/build_vectors.py`); only RTL-SDR recordings are committed (git LFS).

Bluetooth tests: the manifest brings BabbleSim into `tools/bsim` beside the repo. Build it once with `make -C tools/bsim everything -j`, then set `ZEPHYR_BASE`, `BSIM_OUT_PATH=<workspace>/tools/bsim` and `BSIM_COMPONENTS_PATH=$BSIM_OUT_PATH/components` and run `tests/bsim/run.sh`. One image plays the radio (on the native fakes, with the real Bluetooth host and SoftDevice Controller) and three phones; the service logic itself is also tested on `native_sim` with a fake stack (`tests/ble/service`).

XIAO and Renode: the board target is `xiao_ble/nrf52840`. Install the Zephyr SDK minimal bundle and its `arm-zephyr-eabi` toolchain (release v1.0.1 on GitHub, `setup.sh -t arm-zephyr-eabi -c`) and set `ZEPHYR_TOOLCHAIN_VARIANT=zephyr`. `tests/renode/run.sh` runs the image on Renode's nRF52840 (the portable release from GitHub, with `pip install -r <renode>/tests/requirements.txt`) and writes the bench report; `docs/xiao-bench.md` is its output. The image decodes a SAME clip compiled into flash (`tools/vectors/build_clip.py`) until the ADC driver exists.

Later target: a custom `wx_radio` board.

iPhone app: SwiftUI, iOS 17, in `ios/Samewise` (XcodeGen spec; the project isn't committed). Keep every non-view behaviour in `swift/SamewiseKit` so it is tested with `swift test`; the app reaches the radio only through `RadioLink`, with `FakeRadioLink` in tests, previews and the simulator. On Linux, a Swift toolchain from the official `swift` Docker image runs both packages' tests.

## Rules

- `app/` and `services/` include only `hal/*.h`. Driver headers and Zephyr device APIs appear only in `drivers/` and `fakes/`. The one exception is `app/src/main.c`, the composition root: it may include `drivers/` and `fakes/` headers to wire them into an image, and holds no application logic.
- `services/same/` is plain C99: it takes int16 sample buffers and returns results through callbacks.
- No heap in the alert path; static buffers sized in headers.
- Single-precision float is fine (Cortex-M4F); no double in hot loops.
- Every behaviour in the spec gets a test with the code. A bug fix starts with a failing test.
- Test vectors are generated, never hand-edited: regenerate them from `tools/samegen/`.
- The GATT table will live in one header (`services/ble/gatt_table.h`) as the single source of truth, with `docs/gatt.json` generated from it for the iOS mock.
- Never add code that transmits SAME audio over the air.
- Pin the nRF Connect SDK version in `west.yml`; don't bump it without asking.
- If a spec detail is ambiguous or wrong, stop and ask, then update `docs/firmware-spec.md` in the same change.

## SAME reference

- Audio sample rate 10,416.67 Hz (16 MHz ÷ 1,536): exactly 20 samples per bit.
- 520.83 baud; mark 2,083.33 Hz = 1, space 1,562.5 Hz = 0.
- Preamble: 16 bytes of 0xAB. Bytes are 8 bits, least significant bit first, no start or stop bits.
- Header: `ZCZC-ORG-EEE-PSSCCC-PSSCCC+TTTT-JJJHHMM-LLLLLLLL-` with 1 to 31 location codes, sent 3 times about 1 s apart.
- End of message: preamble plus `NNNN`, sent 3 times.
- 47 CFR 11.31 governs the format; check details there, not from memory.
