# Recorded test vectors

RTL-SDR captures of the local weather channel go here, tracked by git LFS
(`*.wav` in `.gitattributes`). Unlike the synthetic vectors, these cannot be
regenerated, so they are committed.

Each clip is a 16-bit mono WAV at 10,417 Hz (16 MHz / 1536, rounded in the
header) with a JSON sidecar of the same name in the samegen format: at least
`expected_headers` (list of header strings, in order) and
`expected_eom_count`. Add a `recorded` group to `tools/vectors/build_vectors.py`
that reads these sidecars, and a suite under `tests/same/` that uses it.
