# Milestone 3.5: real ARM build in Renode

Goal: build the firmware for the XIAO nRF52840 in CI, prove it fits, and run it in Renode to decode SAME audio on the emulated nRF52840 and estimate decoder CPU load. This retires fit and rough-performance risk before hardware arrives; oscillator accuracy, ADC behaviour and exact cycle counts stay in milestone 4.

Done when every check below passes in CI and `docs/xiao-bench.md` records the size, stack and CPU numbers.

## Tasks

1. **XIAO build.** Add a `xiao_ble` build (confirm the board name for the pinned Zephyr). Use real Zephyr subsystems where they exist (Bluetooth, watchdog, flash settings, RTC) and the existing fakes for interfaces without drivers yet (tuner, display, alert_out, battery, input).
2. **Embedded audio source.** An `audio_in` variant that streams a clip compiled into flash instead of reading a host file. Generate it at build time from `tools/samegen/` (one full header with three copies plus EOM, about 8 seconds) so flash use stays reasonable.
3. **Size report.** Print flash and RAM use in the CI job summary. Plan for MCUboot with two image slots: the image must fit in 75% of one slot, and RAM use must stay under 70% of 256 KB, leaving room for real drivers.
4. **Renode run.** A Renode script and Robot Framework test that load the nRF52840 platform and the XIAO image, wait for the boot banner, and wait for the decoded header on the UART. Antmicro publishes nRF52840 Zephyr examples and a GitHub Action for Renode tests; use them if they fit.
5. **CPU estimate.** The firmware prints a start marker before feeding the clip and an end marker after. The Robot test reads Renode's executed-instruction counter at each marker and reports instructions per second of audio, then converts that to CPU load on the 64 MHz core at 1.0, 1.25 and 1.5 cycles per instruction.
6. **Stacks and floats.** Enable Zephyr's thread analyzer in the Renode build and report each thread's stack high-water mark. Build `services/same/` with `-Wdouble-promotion -Werror` so no double-precision math hides in the decoder.

## Checks

- [x] The `xiao_ble` build runs in CI and posts flash and RAM use to the job summary
- [x] The image fits in 75% of one MCUboot slot; RAM use is under 70% of 256 KB
- [x] Renode boots the image and the banner appears on the UART
- [x] The embedded clip decodes on the emulated chip to the same header and EOM as on `native_sim`
- [x] Instructions per second of audio are reported; estimated CPU load at 1.5 cycles per instruction is at most 10%
- [x] No double-precision promotion in `services/same/`
- [x] Every thread's stack high-water mark is at most 75% of its size
- [x] Results are written to `docs/xiao-bench.md`

## Out of scope

Internal-oscillator accuracy, real ADC noise and levels, exact cycle counts, real drivers, the iPhone. Those need the physical XIAO in milestone 4.
