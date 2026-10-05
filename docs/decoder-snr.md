# SAME decoder: decode rate versus SNR

Written by `tests/same/snr` (ztest on `native_sim`); do not edit. Regenerate with
`west twister -T tests/same/snr -p native_sim`.

- Vectors: `tools/vectors/build_vectors.py --group snr`: 50 distinct random valid
  headers (1 to 6 locations), each sent 3 times 1 s apart, 2 s between alerts.
- SNR: AFSK power over white Gaussian noise power across the full band (0 to
  5,208 Hz at 10,416.67 Hz sampling). 0 dB here is about 10 dB Eb/N0.
- Decoded: expected headers reported exactly. Wrong: valid headers reported that
  were never sent (a false alert risk); must be 0 everywhere.
- Gate: 100% at 20 dB and above.

| SNR (dB) | Decoded | Rate | Wrong | Copies framed | Vote failures | Parse failures |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 30 | 50/50 | 100% | 0 | 150/150 | 0 | 0 |
| 27 | 50/50 | 100% | 0 | 150/150 | 0 | 0 |
| 24 | 50/50 | 100% | 0 | 150/150 | 0 | 0 |
| 21 | 50/50 | 100% | 0 | 150/150 | 0 | 0 |
| 18 | 50/50 | 100% | 0 | 150/150 | 0 | 0 |
| 15 | 50/50 | 100% | 0 | 150/150 | 0 | 0 |
| 12 | 50/50 | 100% | 0 | 150/150 | 0 | 0 |
| 9 | 50/50 | 100% | 0 | 150/150 | 0 | 0 |
| 6 | 50/50 | 100% | 0 | 150/150 | 0 | 0 |
| 3 | 50/50 | 100% | 0 | 150/150 | 0 | 0 |
| 0 | 46/50 | 92% | 0 | 150/150 | 2 | 2 |
