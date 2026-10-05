# Milestone 1: SAME decoder on native_sim

Goal: a streaming decoder that turns audio samples into validated SAME headers and end-of-message events, proven by automated tests on `native_sim`. No hardware needed.

Done when every test below passes in CI and `docs/decoder-snr.md` holds a decode-rate-versus-SNR table.

## Tasks

1. **Scaffold.** West manifest pinning the nRF Connect SDK; `app/` builds and runs on `native_sim` and prints a banner; `.github/workflows/ci.yml` runs `west twister -T tests -p native_sim`; git LFS tracks `*.wav`.
2. **Interfaces.** All nine `hal/*.h` headers from the spec: signatures and doc comments only. Implement two fakes now: `audio_in` (WAV reader, any block size) and `clock` (simulated, can run faster than real time).
3. **Generator.** `tools/samegen/`, Python 3 with numpy and scipy only. Phase-continuous AFSK. Inputs: a header string or EOM. Options: sample rate (default 10,416.67 Hz), SNR in dB (white noise), frequency and timing offset in percent, drop copy N, corrupt bytes in copy N, leading and trailing audio (silence, noise or a WAV), a 1050 Hz tone after the header. Output: 16-bit mono WAV plus a JSON sidecar listing expected headers and EOM count.
4. **Decoder.** `services/same/`, streaming:

   ```c
   void same_init(struct same_decoder *d, same_header_cb on_header,
                  same_eom_cb on_eom, void *user);
   void same_feed(struct same_decoder *d, const int16_t *samples, size_t n);
   ```

   Stages per the spec: tone correlators, preamble lock and bit tracking, LSB-first framing, and byte-wise voting across 3 copies (accept 2 matching copies if the third is lost). Emit after the third copy or a timeout; start the timeout at 5 s and tune it with recordings.
5. **Parser.** `struct same_header` with originator, event, up to 31 location codes, purge time, issue time (Julian day, hour, minute) and station ID. Strict validation: fixed field lengths, digits where digits belong. Reject anything malformed.

## Tests (ztest on native_sim)

- [x] Clean header decodes exactly, and the callback fires once
- [x] One corrupted byte in one copy is corrected by voting
- [x] Only two copies received: still decodes
- [x] Only one copy received: not emitted, per the spec (revisit once recordings arrive)
- [x] 31 location codes parse correctly
- [x] Frequency offsets of −2%, −1%, +1% and +2% decode
- [x] SNR sweep from 30 dB down to 0 dB in 3 dB steps writes decode rates to `docs/decoder-snr.md`; initial gate: 100% at 20 dB and above
- [x] EOM is detected; a header followed by EOM fires both callbacks in order
- [x] The same WAV fed in random block sizes from 1 to 512 samples gives identical results
- [x] 10 minutes each of white noise, silence and a 1050 Hz tone produce zero headers
- [x] Malformed headers (wrong field lengths, letters in digit fields) are rejected
- [x] Two different alerts back to back produce two headers

Each item maps to a named test in `tests/same/decoder` (or `tests/same/negative` and `tests/same/snr`); `test_every_vector_matches_its_sidecar` also checks every generated vector against its sidecar.

When RTL-SDR clips arrive in `tools/vectors/recorded/`, add them with sidecars and include them in the suite.

## Out of scope

Event matching, filters, duplicates, alert states, Bluetooth, display and real drivers.

## Later milestones (context only)

- **M2:** event matcher, filter, duplicates, alert state machine, health supervisor; scenario tests with accelerated time.
- **M3:** Bluetooth settings service on `nrf52_bsim`; macOS mock peripheral built from the same GATT table.
- **M4:** XIAO hardware in the loop, RTL-SDR audio into its ADC; check internal-oscillator timing.
- **M5:** real-board drivers and bring-up.
