# Pocket WX Radio firmware

Pocket AM/FM/NOAA weather radio with SAME alerts: Raytac MDBT50Q-1MV2 (nRF52840) plus a Skyworks Si4743 tuner, on Zephyr via the nRF Connect SDK.

- Spec (source of truth): `docs/firmware-spec.md`. Living version: https://claude.ai/code/artifact/58a9a1a9-ee83-45a4-9799-a0010c75ddac
- Current task: `docs/MILESTONE-3.5.md` (milestones 1-3 are done)

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
tools/samegen/      synthetic SAME generator (Python)
tools/vectors/      WAV test vectors (git LFS) with JSON sidecars
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
```

Workspace setup (T2 layout, from the directory containing this repo): `west init -l samewise && west update --narrow -o=--depth=1`. Zephyr in the pinned NCS needs Python 3.12 or newer; install `zephyr/scripts/requirements-{base,build-test,run-test}.txt` and `tools/requirements.txt`. `native_sim` needs `gcc-multilib`, and without the Zephyr SDK set `ZEPHYR_TOOLCHAIN_VARIANT=host`. NCS builds use sysbuild by default, so the app binary lands in `build/app/`. Synthetic test vectors are generated into each test's build directory at build time (`tools/vectors/build_vectors.py`); only RTL-SDR recordings are committed (git LFS).

Bluetooth tests: the manifest brings BabbleSim into `tools/bsim` beside the repo. Build it once with `make -C tools/bsim everything -j`, then set `ZEPHYR_BASE`, `BSIM_OUT_PATH=<workspace>/tools/bsim` and `BSIM_COMPONENTS_PATH=$BSIM_OUT_PATH/components` and run `tests/bsim/run.sh`. One image plays the radio (on the native fakes, with the real Bluetooth host and SoftDevice Controller) and three phones; the service logic itself is also tested on `native_sim` with a fake stack (`tests/ble/service`).

Later targets: `xiao_ble` for the XIAO prototype (confirm the board name for the pinned Zephyr) and a custom `wx_radio` board.

## Rules

- `app/` and `services/` include only `hal/*.h`. Driver headers and Zephyr device APIs appear only in `drivers/` and `fakes/`.
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
