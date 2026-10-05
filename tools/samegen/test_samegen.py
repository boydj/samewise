"""Self-tests for samegen. Run: python3 -m unittest discover -s tools -p 'test_*.py'"""

import importlib.util
import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from scipy.io import wavfile

# Load samegen.py by path: "samegen" is also this package's name under discovery.
_spec = importlib.util.spec_from_file_location("samegen_module", Path(__file__).with_name("samegen.py"))
sg = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = sg  # dataclasses look the module up by name
_spec.loader.exec_module(sg)

HDR = "ZCZC-WXR-TOR-048453-048491+0030-2781915-KEWX/NWS-"


def demod_bytes(samples, start, n_bytes, fs=sg.SAME_RATE):
    """Reference demodulator: correlate whole bits at known timing."""
    spb = fs / sg.BAUD
    x = samples.astype(np.float64)
    bits = []
    for k in range(n_bytes * 8):
        a = int(round(start + k * spb))
        seg = x[a:a + int(round(spb))]
        t = np.arange(len(seg)) / fs
        e = [abs(np.sum(seg * np.exp(-2j * np.pi * f * t))) for f in (sg.SPACE_HZ, sg.MARK_HZ)]
        bits.append(int(e[1] > e[0]))
    return np.packbits(np.array(bits, dtype=np.uint8).reshape(-1, 8), axis=1, bitorder="little").reshape(-1).tobytes()


def peak_hz(x, fs):
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)), n=1 << 18))
    return np.argmax(spec) * fs / (1 << 18)


class Constants(unittest.TestCase):
    def test_timing_matches_reference(self):
        self.assertAlmostEqual(sg.SAME_RATE, 10416.6667, places=3)
        self.assertAlmostEqual(sg.BAUD, 520.8333, places=3)
        self.assertAlmostEqual(sg.MARK_HZ, 2083.3333, places=3)
        self.assertAlmostEqual(sg.SPACE_HZ, 1562.5, places=6)
        self.assertEqual(sg.PREAMBLE, b"\xab" * 16)

    def test_bits_are_lsb_first(self):
        self.assertEqual(sg.byte_bits(b"\xab").tolist(), [1, 1, 0, 1, 0, 1, 0, 1])
        self.assertEqual(sg.byte_bits(b"Z").tolist(), [0, 1, 0, 1, 1, 0, 1, 0])


class Waveform(unittest.TestCase):
    def test_tone_frequencies_and_offset(self):
        for scale in (1.0, 1.02, 0.98):
            mark = sg.afsk(np.ones(400, dtype=np.uint8), sg.SAME_RATE, 0.5, freq_scale=scale)
            space = sg.afsk(np.zeros(400, dtype=np.uint8), sg.SAME_RATE, 0.5, freq_scale=scale)
            self.assertAlmostEqual(peak_hz(mark, sg.SAME_RATE), sg.MARK_HZ * scale, delta=1.0)
            self.assertAlmostEqual(peak_hz(space, sg.SAME_RATE), sg.SPACE_HZ * scale, delta=1.0)

    def test_exactly_20_samples_per_bit(self):
        bits = sg.byte_bits(b"\xab\x00\xff")
        self.assertEqual(len(sg.afsk(bits, sg.SAME_RATE, 0.5)), 24 * 20)
        self.assertEqual(len(sg.afsk(bits, sg.SAME_RATE, 0.5, baud_scale=1.02)), math.ceil(24 * 20 / 1.02))

    def test_phase_continuous(self):
        x = sg.afsk(sg.byte_bits(b"ZCZC-\xab\x55"), sg.SAME_RATE, 0.5)
        max_step = 0.5 * 2 * np.pi * sg.MARK_HZ / sg.SAME_RATE
        self.assertLessEqual(np.max(np.abs(np.diff(x))), max_step + 1e-9)

    def test_reference_demodulator_recovers_copies(self):
        for timing in (0.0, 1.0, -1.0):
            samples, side = sg.generate([HDR], sg.Options(lead_s=0.2, timing_offset_pct=timing))
            for c in side["messages"][0]["copies"]:
                got = demod_bytes_timing(samples, c["start_sample"], len(sg.PREAMBLE) + len(HDR), timing)
                self.assertEqual(got, sg.PREAMBLE + HDR.encode(), f"copy {c['copy']} timing {timing}")


def demod_bytes_timing(samples, start, n_bytes, timing_pct):
    """Reference demodulator for a baud-rate offset: 18-sample windows inside each bit."""
    spb = 20 / (1 + timing_pct / 100)
    x = samples.astype(np.float64)
    bits = []
    for k in range(n_bytes * 8):
        a = int(round(start + k * spb + 1))
        seg = x[a:a + 18]
        t = np.arange(len(seg)) / sg.SAME_RATE
        e = [abs(np.sum(seg * np.exp(-2j * np.pi * f * t))) for f in (sg.SPACE_HZ, sg.MARK_HZ)]
        bits.append(int(e[1] > e[0]))
    return np.packbits(np.array(bits, dtype=np.uint8).reshape(-1, 8), axis=1, bitorder="little").reshape(-1).tobytes()


class Layout(unittest.TestCase):
    def test_three_copies_one_second_apart(self):
        samples, side = sg.generate([HDR], sg.Options(lead_s=0.5))
        copies = side["messages"][0]["copies"]
        self.assertEqual([c["copy"] for c in copies], [1, 2, 3])
        burst = (16 + len(HDR)) * 8 * 20
        for i, c in enumerate(copies):
            self.assertEqual(c["samples"], burst)
            self.assertEqual(c["start_sample"], round(0.5 * sg.SAME_RATE) + i * (burst + round(sg.SAME_RATE)))

    def test_dropped_copy_is_silent(self):
        samples, side = sg.generate([HDR], sg.Options(drop=[(None, 2)]))
        c = side["messages"][0]["copies"][1]
        self.assertTrue(c["dropped"])
        self.assertFalse(np.any(samples[c["start_sample"]:c["start_sample"] + c["samples"]]))

    def test_eighth_bit(self):
        samples, side = sg.generate([HDR], sg.Options(eighth_bit=True, lead_s=0))
        got = demod_bytes(samples, 0, 16 + len(HDR))
        self.assertEqual(got[:16], sg.PREAMBLE, "preamble is unchanged")
        self.assertEqual(got[16:], bytes(b | 0x80 for b in HDR.encode()))
        self.assertEqual(side["expected_headers"], [HDR])

    def test_tone1050_follows_header(self):
        samples, side = sg.generate([HDR], sg.Options(tone1050_s=2.0, trail_s=0))
        tail = samples[-int(2.0 * sg.SAME_RATE):].astype(float)
        self.assertAlmostEqual(peak_hz(tail, sg.SAME_RATE), 1050.0, delta=1.0)


class Noise(unittest.TestCase):
    def test_snr_is_full_band(self):
        for snr in (20.0, 6.0, 0.0):
            opts = sg.Options(snr_db=snr, lead_s=0, trail_s=0, amplitude=0.25, seed=3)
            clean, side = sg.generate([HDR], sg.Options(lead_s=0, trail_s=0, amplitude=0.25))
            noisy, _ = sg.generate([HDR], opts)
            sig = clean.astype(float)
            noise = noisy.astype(float) - sig
            on = np.zeros(len(sig), dtype=bool)  # only where the AFSK is on (gaps are silent)
            for c in side["messages"][0]["copies"]:
                on[c["start_sample"]:c["start_sample"] + c["samples"]] = True
            measured = 10 * np.log10(np.mean(sig[on] ** 2) / np.mean(noise ** 2))
            self.assertAlmostEqual(measured, snr, delta=0.2)

    def test_same_seed_same_output(self):
        a, _ = sg.generate([HDR], sg.Options(snr_db=5, seed=7, corrupt=[(None, 1, 2)]))
        b, _ = sg.generate([HDR], sg.Options(snr_db=5, seed=7, corrupt=[(None, 1, 2)]))
        c, _ = sg.generate([HDR], sg.Options(snr_db=5, seed=8, corrupt=[(None, 1, 2)]))
        self.assertTrue(np.array_equal(a, b))
        self.assertFalse(np.array_equal(a, c))

    def test_lead_kinds(self):
        for kind in ("silence", "noise", "tone1050"):
            samples, _ = sg.generate([], sg.Options(lead=kind, lead_s=2.0, trail_s=0))
            self.assertEqual(len(samples), round(2.0 * sg.SAME_RATE))
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "src.wav"
            wavfile.write(src, 8000, (np.sin(np.arange(8000) / 3) * 10000).astype(np.int16))
            samples, _ = sg.generate([], sg.Options(lead=str(src), trail_s=0))
            self.assertAlmostEqual(len(samples), sg.SAME_RATE, delta=2)  # 1 s, resampled


class Expectations(unittest.TestCase):
    def expected(self, msgs, **kw):
        return sg.generate(msgs, sg.Options(**kw))[1]

    def test_clean(self):
        side = self.expected([HDR])
        self.assertEqual(side["expected_headers"], [HDR])
        self.assertEqual(side["expected_eom_count"], 0)

    def test_one_corrupt_copy_is_voted_out(self):
        side = self.expected([HDR], corrupt=[(None, 2, 3)])
        self.assertEqual(len(side["messages"][0]["copies"][1]["corrupted_positions"]), 3)
        self.assertEqual(side["expected_headers"], [HDR])

    def test_two_copies_suffice_one_does_not(self):
        self.assertEqual(self.expected([HDR], drop=[(None, 1)])["expected_headers"], [HDR])
        self.assertEqual(self.expected([HDR], drop=[(None, 1), (None, 3)])["expected_headers"], [])

    def test_two_copies_that_disagree_are_rejected(self):
        side = self.expected([HDR], drop=[(None, 3)], corrupt=[(None, 2, 1)])
        self.assertEqual(side["expected_headers"], [])

    def test_message_scoped_options(self):
        other = HDR.replace("TOR", "SVR")
        side = self.expected([HDR, other], drop=[(2, 1), (2, 2)])
        self.assertEqual(side["expected_headers"], [HDR])

    def test_eom(self):
        side = self.expected([HDR, "EOM"])
        self.assertEqual(side["expected_headers"], [HDR])
        self.assertEqual(side["expected_eom_count"], 1)
        self.assertEqual(self.expected(["EOM"], drop=[(None, 1), (None, 2)])["expected_eom_count"], 1)
        self.assertEqual(self.expected(["EOM"], drop=[(None, 1), (None, 2), (None, 3)])["expected_eom_count"], 0)

    def test_malformed_not_expected(self):
        self.assertEqual(self.expected([HDR.replace("048453", "04845A")])["expected_headers"], [])


class Validation(unittest.TestCase):
    def test_valid(self):
        for h in (HDR, "ZCZC-EAS-RWT-000000+0015-0010000-WXYZ AM -",
                  "ZCZC-CIV-CAE-" + "-".join(f"{i:06d}" for i in range(31)) + "+0600-3662359-KABC/FM -"):
            self.assertTrue(sg.is_valid_header(h), h)

    def test_invalid(self):
        bad = [
            HDR.replace("048453", "04845"),         # short location
            HDR.replace("048453", "0484531"),       # long location
            HDR.replace("048453", "04845A"),        # letter in digits
            HDR.replace("+0030", "+00A0"),          # letter in purge
            HDR.replace("+0030", "+0060"),          # purge minutes
            HDR.replace("2781915", "3671915"),      # Julian day 367
            HDR.replace("2781915", "0001915"),      # Julian day 0
            HDR.replace("2781915", "2782415"),      # hour 24
            HDR.replace("2781915", "2781960"),      # minute 60
            HDR.replace("KEWX/NWS", "KEWX/NW"),     # station too short
            HDR.replace("KEWX/NWS", "KEWX-NWS"),    # dash in station
            HDR.replace("WXR", "wxr"),              # lower case
            HDR.replace("TOR", "T0R"),              # digit in event
            HDR[:-1],                               # no closing dash
            "ZCZC-WXR-TOR-" + "-".join(["048453"] * 32) + "+0030-2781915-KEWX/NWS-",  # 32 locations
            "ZCZC-WXR-TOR-+0030-2781915-KEWX/NWS-",  # no locations
            "NNNN",
        ]
        for h in bad:
            self.assertFalse(sg.is_valid_header(h), h)


class Cli(unittest.TestCase):
    def test_writes_wav_and_sidecar(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "x.wav"
            self.assertEqual(sg.main([HDR, "EOM", "-o", str(out), "--snr", "20", "--drop", "1/3",
                                      "--corrupt", "2/1:1", "--seed", "4"]), 0)
            rate, data = wavfile.read(out)
            side = json.loads(out.with_suffix(".json").read_text())
            self.assertEqual(rate, 10417)
            self.assertEqual(data.dtype, np.int16)
            self.assertEqual(len(data), side["samples"])
            self.assertEqual(side["expected_headers"], [HDR])
            self.assertEqual(side["expected_eom_count"], 1)
            self.assertEqual(side["options"]["drop"], [[1, 3]])


if __name__ == "__main__":
    unittest.main()
