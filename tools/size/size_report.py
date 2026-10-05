#!/usr/bin/env python3
"""Flash and RAM use of a firmware image against the milestone 3.5 budget.

The budget plans for MCUboot with two image slots on the nRF52840:

  1024 KB flash = 48 KB MCUboot + 2 x 472 KB slots + 32 KB settings storage

The image must fit in 75% of one slot, and RAM use must stay under 70% of
256 KB, leaving room for the real drivers. Flash use is what gets
programmed (the file size of every loadable segment in flash); RAM use is
every loadable segment's size in RAM (data, bss and noinit, stacks
included).

  size_report.py build/app/zephyr/zephyr.elf [--summary FILE] [--json FILE]

Prints a Markdown table, appends it to --summary (GitHub's
$GITHUB_STEP_SUMMARY), and exits 1 if either budget is exceeded.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

KB = 1024
FLASH_SIZE = 1024 * KB
MCUBOOT = 48 * KB
STORAGE = 32 * KB
SLOT = (FLASH_SIZE - MCUBOOT - STORAGE) // 2
FLASH_BUDGET = SLOT * 75 // 100
RAM_SIZE = 256 * KB
RAM_BUDGET = RAM_SIZE * 70 // 100

FLASH_BASE, RAM_BASE = 0x0000_0000, 0x2000_0000


@dataclass
class Segment:
    paddr: int  # load address
    vaddr: int  # run address
    filesz: int
    memsz: int


@dataclass
class Report:
    flash: int
    ram: int
    clip: int  # the embedded SAME clip, part of flash
    slot: int = SLOT
    flash_budget: int = FLASH_BUDGET
    ram_size: int = RAM_SIZE
    ram_budget: int = RAM_BUDGET

    @property
    def ok(self) -> bool:
        return self.flash <= self.flash_budget and self.ram <= self.ram_budget


def usage(segments: list[Segment]) -> tuple[int, int]:
    """(flash bytes, RAM bytes) from an image's loadable segments."""
    flash = sum(s.filesz for s in segments if FLASH_BASE <= s.paddr < FLASH_BASE + FLASH_SIZE)
    ram = sum(s.memsz for s in segments if RAM_BASE <= s.vaddr < RAM_BASE + RAM_SIZE)
    return flash, ram


def read_elf(path: Path) -> tuple[list[Segment], int]:
    from elftools.elf.elffile import ELFFile  # pyelftools, in Zephyr's requirements

    with path.open("rb") as f:
        elf = ELFFile(f)
        segs = [Segment(p["p_paddr"], p["p_vaddr"], p["p_filesz"], p["p_memsz"])
                for p in elf.iter_segments() if p["p_type"] == "PT_LOAD"]
        clip = 0
        symtab = elf.get_section_by_name(".symtab")
        if symtab is not None:
            for sym in symtab.get_symbol_by_name("wx_clip_samples") or []:
                clip = sym["st_size"]
    return segs, clip


def kb(n: int) -> str:
    return f"{n / KB:.1f} KB"


def markdown(r: Report, title: str) -> str:
    def row(name, used, budget, total):
        mark = "pass" if used <= budget else "**over**"
        return f"| {name} | {kb(used)} | {kb(budget)} | {used * 100 / total:.1f}% of {kb(total)} | {mark} |"

    return "\n".join([
        f"### {title}", "",
        "| | Used | Budget | Share | |",
        "| --- | --- | --- | --- | --- |",
        row("Flash", r.flash, r.flash_budget, r.slot),
        row("RAM", r.ram, r.ram_budget, r.ram_size),
        "",
        f"Flash budget: 75% of one {kb(r.slot)} MCUboot slot (1 MB = 48 KB MCUboot + 2 slots + "
        f"32 KB storage). RAM budget: 70% of 256 KB. The embedded SAME clip is {kb(r.clip)} of the flash.",
        "",
    ])


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("elf", type=Path)
    p.add_argument("--title", default="Firmware size (xiao_ble/nrf52840)")
    p.add_argument("--summary", type=Path, help="append the table here (GITHUB_STEP_SUMMARY)")
    p.add_argument("--json", type=Path, help="write the numbers here")
    a = p.parse_args(argv)
    segs, clip = read_elf(a.elf)
    flash, ram = usage(segs)
    r = Report(flash, ram, clip)
    text = markdown(r, a.title)
    print(text)
    if a.summary:
        with a.summary.open("a") as f:
            f.write(text + "\n")
    if a.json:
        a.json.write_text(json.dumps({**asdict(r), "ok": r.ok}, indent=2) + "\n")
    return 0 if r.ok else 1


if __name__ == "__main__":
    sys.exit(main())
