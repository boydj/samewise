# Milestone 3.6: screens and drivers before hardware

Goal: draw every screen the radio shows from the screen model, in the design's style, and write the drivers for the tuner, fuel gauge and charger against emulated chips. All of it is tested on `native_sim`, so milestone 4 starts on the XIAO with code that has already run.

Done when every check below passes in CI, renders of every screen sit next to the design in the README, and the drivers pass their tests against the emulators.

## Decisions

- The listening screen draws whatever band, frequency, RDS name, preset and volume the radio reports. Tuning behaviour (steps, seek, preset recall, volume) gets its own spec section and milestone.
- The font is VT323, the design's pixel font (SIL Open Font License 1.1). A script in `tools/fonts/` turns it into bitmaps; the generated C file is committed and CI checks that it is current.
- Screens the design doesn't cover (ALERTS OFF, RESTARTED, the reconnect window, the confirmation before a third phone replaces one) are laid out in the same style and reviewed in the PR.
- Every chip detail in a driver or emulator cites its source document and section: AN332 (Si47xx Programming Guide) and the Si4743 datasheet for the tuner, the BQ25180 datasheet for the charger. The MAX17048 uses Zephyr's driver and Zephyr's emulator. An emulator built from the same reading as its driver shares that reading's mistakes, so milestone 4 rechecks each driver on the board.

## Tasks

1. **Housekeeping.** Tick the spec's scenario tests, which `tests/scenario` already covers.
2. **Font.** `tools/fonts/build_fonts.py` rasterises VT323 at the sizes the screens use into 1-bit glyph bitmaps, writing `app/services/gfx/font_vt323.c`. `--check` fails if the committed file is stale. Only printable ASCII.
3. **Drawing.** `services/gfx/`, plain C99 like `services/same/`: text in a font (left, centred, right), filled and inverted rectangles, and lines, into the HAL framebuffer layout (`hal/display.h`). No heap. Unit tests for clipping, alignment and inversion.
4. **Screen model.** Add what the screens show but the model lacks: band, frequency, stereo, signal bars, RDS name, preset and volume for listening; weather channel, county count, filter and last alert for standby; battery days left; the Bluetooth window's seconds left. Each field is filled by its owner (radio, health, Bluetooth service) and tested there.
5. **Screens.** `app/src/app/screens.c` draws `struct ui_model` into the framebuffer: standby, listening, alert (newest; test alerts read Practice/Demo Warning), the warning family (no signal, no weekly test, tuner fault, battery low and critical), ALERTS OFF, RESTARTED, and the Bluetooth screens (pairing passkey, reconnect window, third-phone confirmation). Long event names and RDS text are cut to fit, never wrapped off-screen.
6. **Display driver and fake.** `fakes/display_fake.c` keeps the framebuffer in RAM, counts flushes and can write a PBM image. `drivers/display_zephyr.c` implements `hal/display.h` on Zephyr's display API, sending changed rows to the panel (the `sharp,ls0xx` driver on the board; SDL on `native_sim` for a desktop window). A test checks the pixel conversion with a recording display device.
7. **UI thread.** Redraw from low-priority work, never from the alert path. Redraw at once when the screen or its data changes, and otherwise at most once a minute in standby. Backlight on for 5 seconds per key press or alert (spec: Power modes). A scenario test checks the alert path never waits on a flush.
8. **Golden images.** `tests/display/screens` renders each screen from scripted models and compares it to committed PBM images in the test's `golden/` folder. `--update` regenerates them, never hand edits. `tools/display/render_png.py` scales them up for `docs/images/screens/` and the README.
9. **Fuel gauge and charger.** `drivers/battery_zephyr.c` implements `hal/battery.h` on Zephyr's MAX17048 fuel-gauge driver and its BQ25180 charger driver, plus ship mode and temperature fault from the charger's registers. Tests on `native_sim` run it against Zephyr's MAX17048 emulator and a BQ25180 emulator in `tests/drivers/`.
10. **Tuner.** `drivers/tuner_si4743.c` implements `hal/tuner.h` over I2C: power up and down, band select, tune, seek, signal status, mute and RDS radio text. Tests on `native_sim` run it against an Si4743 emulator in `tests/drivers/` that follows AN332's command and response layouts, including CTS polling, error status and seek wrap.
11. **Docs.** The spec gains a Screens section listing every screen and what it shows, and its hardware table names the drivers. `CLAUDE.md` adds `services/gfx/`, the font tool and the driver tests.

## Checks

- [ ] `build_fonts.py --check` passes in CI, and the generated font matches VT323 at each size
- [ ] Every `services/gfx/` function is unit tested, including clipping at all four edges
- [ ] Every screen in task 5 has a golden image, and the test fails on any changed pixel
- [ ] Text never overflows: the longest event name in the table, a 64-character RDS text and 31 matched counties all fit their screens
- [ ] Standby redraws at most once a minute; a change of screen or data redraws at once
- [ ] An alert decoded during a slow display flush still sounds without waiting for it
- [ ] The display driver's conversion to the panel's format is tested
- [ ] `hal/battery.h` on the MAX17048 and BQ25180 drivers passes against the emulators, including temperature fault and ship mode
- [ ] `hal/tuner.h` on the Si4743 driver passes against the emulator: each band, tune limits, seek found and not found, status, mute, RDS, I2C errors as -EIO
- [ ] Renders of every screen are in the README next to the design
- [ ] The XIAO build still fits its flash and RAM budget

## Out of scope

Tuning behaviour (task 4 only displays what the radio reports), the buzzer, vibration and headphone amp drivers, the SAADC driver, and anything that needs the board: milestone 4.
