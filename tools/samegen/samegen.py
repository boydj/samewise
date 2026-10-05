#!/usr/bin/env python3
"""samegen: synthetic SAME audio for the decoder tests.

Builds the AFSK bursts of 47 CFR 11.31 (520.83 baud, mark 2083.33 Hz = 1,
space 1562.5 Hz = 0, 16-byte 0xAB preamble, 8-bit bytes sent least
significant bit first, each message sent 3 times about 1 s apart) and writes
a 16-bit mono WAV plus a JSON sidecar listing the headers and end-of-message
events a correct decoder should report.

This tool exists to feed the decoder on a laptop. Never transmit its output
over the air.

Messages are header strings ("ZCZC-...-") or the word EOM. Copies are
numbered 1 to 3 and messages from 1; options that take MSG/COPY apply to
every message when MSG/ is left out.

Examples:
  samegen.py 'ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS-' -o tor.wav
  samegen.py 'ZCZC-WXR-SVR-048453+0045-2781930-KEWX/NWS-' EOM --tone1050 8 \\
      --snr 12 --freq-offset -2 --drop 3 -o svr.wav
  samegen.py --lead noise --lead-seconds 600 -o noise.wav
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from dataclasses import asdict, dataclass, field
from fractions import Fraction
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly

SAME_RATE = 16_000_000 / 1536  # 10,416.67 Hz: exactly 20 samples per bit
BAUD = SAME_RATE / 20  # 520.83 bit/s
MARK_HZ = 4 * BAUD  # 2083.33 Hz, bit 1
SPACE_HZ = 3 * BAUD  # 1562.5 Hz, bit 0
PREAMBLE = b"\xab" * 16
EOM_TEXT = "NNNN"
ATTENTION_HZ = 1050.0
MAX_LOCATIONS = 31

# Printable ASCII except '-' for the 8-character station ID.
_HEADER_RE = re.compile(
    r"^ZCZC-([A-Z]{3})-([A-Z]{3})-((?:\d{6}-){0,30}\d{6})\+(\d{4})-(\d{7})-"
    r"([\x20-\x2c\x2e-\x7e]{8})-$"
)


def is_valid_header(text: str) -> bool:
    """Strict header validation, kept independent of the C parser as an oracle.

    Fixed field lengths, digits where digits belong, 1 to 31 locations,
    purge minutes 00-59, Julian day 001-366, hour 00-23, minute 00-59.
    """
    m = _HEADER_RE.match(text)
    if not m:
        return False
    purge, issue = m.group(4), m.group(5)
    day, hour, minute = int(issue[0:3]), int(issue[3:5]), int(issue[5:7])
    return int(purge[2:4]) < 60 and 1 <= day <= 366 and hour < 24 and minute < 60


def byte_bits(data: bytes) -> np.ndarray:
    """Bits of each byte, least significant first, no start or stop bits."""
    arr = np.frombuffer(bytes(data), dtype=np.uint8)
    return np.unpackbits(arr[:, None], axis=1, bitorder="little").reshape(-1)


def afsk(bits: np.ndarray, fs: float, amplitude: float, freq_scale: float = 1.0,
         baud_scale: float = 1.0) -> np.ndarray:
    """Phase-continuous AFSK for a bit sequence, starting at phase 0."""
    baud = BAUD * baud_scale
    n = int(math.ceil(len(bits) * fs / baud - 1e-9))  # fs / baud is 20.000000000000004
    # Bit index of each sample; the epsilon keeps exact boundaries exact.
    k = np.minimum(np.floor(np.arange(n) * (baud / fs) + 1e-9).astype(np.int64), len(bits) - 1)
    freq = np.where(bits[k] == 1, MARK_HZ, SPACE_HZ) * freq_scale
    phase = 2 * np.pi * (np.cumsum(freq) - freq) / fs
    return amplitude * np.sin(phase)


@dataclass
class Options:
    rate: float = SAME_RATE
    snr_db: float | None = None  # white noise over the full band, 0 to rate/2
    freq_offset_pct: float = 0.0  # tone frequency error
    timing_offset_pct: float = 0.0  # baud rate error (+ = faster bits)
    drop: list = field(default_factory=list)  # [(msg or None, copy)]
    corrupt: list = field(default_factory=list)  # [(msg or None, copy, n_bytes)]
    lead: str = "silence"  # silence | noise | tone1050 | path to a WAV
    lead_s: float = 1.0
    trail: str = "silence"
    trail_s: float = 1.0
    tone1050_s: float = 0.0  # attention tone after each header's copies
    gap_s: float = 1.0  # between copies
    message_gap_s: float = 2.0  # between messages
    amplitude: float = 0.5  # AFSK peak, fraction of full scale
    eighth_bit: bool = False  # send characters with the 8th (null) bit set
    seed: int = 0


def _applies(spec_msg, msg_no: int) -> bool:
    return spec_msg is None or spec_msg == msg_no


def _fill(kind: str, seconds: float, opts: Options, rng: np.random.Generator) -> np.ndarray:
    n = int(round(seconds * opts.rate))
    if kind == "silence":
        return np.zeros(n)
    if kind == "noise":
        return rng.normal(0.0, opts.amplitude / math.sqrt(2), n)  # same power as the AFSK
    if kind == "tone1050":
        return opts.amplitude * np.sin(2 * np.pi * ATTENTION_HZ * np.arange(n) / opts.rate)
    # A WAV file, used whole and resampled to the output rate.
    src_rate, raw = wavfile.read(kind)
    data = raw.astype(np.float64)
    if np.issubdtype(raw.dtype, np.integer):
        data /= float(np.iinfo(raw.dtype).max) + 1.0
    if data.ndim > 1:
        data = data.mean(axis=1)
    ratio = Fraction(opts.rate / src_rate).limit_denominator(2000)
    if ratio != 1:
        data = resample_poly(data, ratio.numerator, ratio.denominator)
    return data


def _vote(copies: list) -> bytes | None:
    """Byte-wise majority over the copies received: at least 2 must agree everywhere."""
    present = [c for c in copies if c is not None]
    if len(present) < 2:
        return None
    out = bytearray()
    for i in range(max(len(c) for c in present)):
        vals = [c[i] & 0x7F for c in present if i < len(c)]
        best = max(set(vals), key=vals.count)
        if vals.count(best) < 2:
            return None
        out.append(best)
    return bytes(out)


def generate(messages: list[str], opts: Options | None = None):
    """Render messages to audio and describe what a decoder should find.

    Returns (int16 samples, sidecar dict).
    """
    opts = opts or Options()
    rng = np.random.default_rng(opts.seed)
    fs = opts.rate
    fscale = 1 + opts.freq_offset_pct / 100
    bscale = 1 + opts.timing_offset_pct / 100

    parts = [_fill(opts.lead, opts.lead_s, opts, rng)]
    pos = len(parts[0])
    described, expected_headers, eom_count = [], [], 0

    for msg_no, msg in enumerate(messages, start=1):
        is_eom = msg.upper() == "EOM"
        text = EOM_TEXT if is_eom else msg
        raw = text.encode("ascii")
        if opts.eighth_bit:
            raw = bytes(b | 0x80 for b in raw)

        received, copies = [], []
        for copy_no in (1, 2, 3):
            data = bytearray(raw)
            dropped = any(_applies(m, msg_no) and c == copy_no for m, c in opts.drop)
            n_bad = sum(k for m, c, k in opts.corrupt if _applies(m, msg_no) and c == copy_no)
            bad_pos = sorted(rng.choice(len(data), size=min(n_bad, len(data)), replace=False).tolist()) if n_bad else []
            for p in bad_pos:
                data[p] ^= int(rng.integers(1, 128))  # always changes the 7-bit character
            burst = afsk(byte_bits(PREAMBLE + bytes(data)), fs, opts.amplitude, fscale, bscale)
            if dropped:
                burst = np.zeros_like(burst)  # sent, but lost to the receiver
            copies.append({"copy": copy_no, "start_sample": pos, "samples": len(burst),
                           "dropped": dropped, "corrupted_positions": bad_pos})
            received.append(None if dropped else bytes(data))
            parts.append(burst)
            pos += len(burst)
            if copy_no < 3:
                gap = np.zeros(int(round(opts.gap_s * fs)))
                parts.append(gap)
                pos += len(gap)

        if is_eom:
            # One EOM copy is enough: at least 3 of its 4 characters must read N.
            ok = any(r is not None and sum((b & 0x7F) == ord("N") for b in r[:4]) >= 3 for r in received)
            eom_count += int(ok)
        else:
            voted = _vote(received)
            if voted is not None and is_valid_header(voted.decode("ascii")):
                expected_headers.append(voted.decode("ascii"))
            if opts.tone1050_s > 0:
                gap = np.zeros(int(round(opts.gap_s * fs)))
                tone = _fill("tone1050", opts.tone1050_s, opts, rng)
                parts += [gap, tone]
                pos += len(gap) + len(tone)

        described.append({"type": "eom" if is_eom else "header", "text": text,
                          "valid": is_eom or is_valid_header(text), "copies": copies})
        if msg_no < len(messages):
            gap = np.zeros(int(round(opts.message_gap_s * fs)))
            parts.append(gap)
            pos += len(gap)

    parts.append(_fill(opts.trail, opts.trail_s, opts, rng))
    audio = np.concatenate(parts)

    if opts.snr_db is not None:
        sigma = (opts.amplitude / math.sqrt(2)) / (10 ** (opts.snr_db / 20))
        audio = audio + rng.normal(0.0, sigma, len(audio))

    scaled = np.round(audio * 32767.0)
    clipped = int(np.count_nonzero((scaled > 32767) | (scaled < -32768)))
    samples = np.clip(scaled, -32768, 32767).astype(np.int16)

    sidecar = {
        "generator": "samegen",
        "format": 1,
        "sample_rate": fs,
        "wav_sample_rate": int(round(fs)),
        "samples": len(samples),
        "clipped_samples": clipped,
        "snr_definition": "AFSK power over white-noise power across 0 to sample_rate/2",
        "options": asdict(opts),
        "messages": described,
        "expected_headers": expected_headers,
        "expected_eom_count": eom_count,
    }
    return samples, sidecar


def write(path: Path, samples: np.ndarray, sidecar: dict) -> Path:
    """Write the WAV and its sidecar (same name, .json). Returns the sidecar path."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    wavfile.write(path, sidecar["wav_sample_rate"], samples)
    side = path.with_suffix(".json")
    side.write_text(json.dumps(sidecar, indent=2) + "\n")
    return side


def _msg_copy(spec: str):
    msg, _, copy = spec.rpartition("/")
    copy_no = int(copy)
    if copy_no not in (1, 2, 3):
        raise argparse.ArgumentTypeError("copy must be 1, 2 or 3")
    return (int(msg) if msg else None), copy_no


def _drop(spec: str):
    return _msg_copy(spec)


def _corrupt(spec: str):
    where, _, count = spec.partition(":")
    msg, copy_no = _msg_copy(where)
    return msg, copy_no, int(count) if count else 1


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("messages", nargs="*", metavar="MESSAGE", help="header text, or EOM")
    p.add_argument("-o", "--output", required=True, type=Path, help="output WAV; sidecar goes beside it")
    p.add_argument("--rate", type=float, default=SAME_RATE, help="sample rate in Hz (default 10416.67)")
    p.add_argument("--snr", type=float, help="SNR in dB, white noise over the full band")
    p.add_argument("--freq-offset", type=float, default=0.0, metavar="PCT", help="tone frequency error, percent")
    p.add_argument("--timing-offset", type=float, default=0.0, metavar="PCT", help="baud rate error, percent")
    p.add_argument("--drop", type=_drop, action="append", default=[], metavar="[MSG/]COPY",
                   help="drop a copy (repeatable)")
    p.add_argument("--corrupt", type=_corrupt, action="append", default=[], metavar="[MSG/]COPY[:N]",
                   help="corrupt N random bytes of a copy (default 1; repeatable)")
    p.add_argument("--lead", default="silence", help="silence, noise, tone1050 or a WAV path")
    p.add_argument("--lead-seconds", type=float, default=1.0)
    p.add_argument("--trail", default="silence", help="silence, noise, tone1050 or a WAV path")
    p.add_argument("--trail-seconds", type=float, default=1.0)
    p.add_argument("--tone1050", type=float, default=0.0, metavar="SECONDS",
                   help="1050 Hz attention tone after each header")
    p.add_argument("--gap", type=float, default=1.0, metavar="SECONDS", help="gap between copies")
    p.add_argument("--message-gap", type=float, default=2.0, metavar="SECONDS")
    p.add_argument("--amplitude", type=float, default=0.5, help="AFSK peak as a fraction of full scale")
    p.add_argument("--eighth-bit", action="store_true", help="set the 8th (null) bit of each character")
    p.add_argument("--seed", type=int, default=0)
    a = p.parse_args(argv)

    opts = Options(rate=a.rate, snr_db=a.snr, freq_offset_pct=a.freq_offset,
                   timing_offset_pct=a.timing_offset, drop=a.drop, corrupt=a.corrupt,
                   lead=a.lead, lead_s=a.lead_seconds, trail=a.trail, trail_s=a.trail_seconds,
                   tone1050_s=a.tone1050, gap_s=a.gap, message_gap_s=a.message_gap,
                   amplitude=a.amplitude, eighth_bit=a.eighth_bit, seed=a.seed)
    samples, sidecar = generate(a.messages, opts)
    side = write(a.output, samples, sidecar)
    print(f"{a.output}: {len(samples)} samples, {len(sidecar['expected_headers'])} expected header(s), "
          f"{sidecar['expected_eom_count']} EOM; sidecar {side}")
    if sidecar["clipped_samples"]:
        print(f"warning: {sidecar['clipped_samples']} samples clipped", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
