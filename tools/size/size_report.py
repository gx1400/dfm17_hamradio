#!/usr/bin/env python3
"""size_report.py: firmware flash/RAM report, size budget and soft-double check.

    size_report.py [--base DIR] [--head DIR] [--prefix arm-none-eabi-]

Reads every variant built into DIR/<variant>/dfm17.elf (default head: build/).
With --base (e.g. a build of main), it shows each variant's change and the
symbols that grew or shrank. Prints Markdown, appends it to
$GITHUB_STEP_SUMMARY when set and to --markdown FILE if given (CI's PR
comment), and emits GitHub annotations in CI.

Exit status 1 if a variant is over budget or links the software
double-precision routines. Budgets are set here, in one place (issue #22).
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

# STM32F100R8: 64 KB flash, 8 KB RAM
FLASH_TOTAL = 64 * 1024
RAM_TOTAL = 8 * 1024
# budgets: fail above these. Flash leaves room for the EEPROM settings work
# (#18); RAM includes the linker's reserved heap + stack (._user_heap_stack)
FLASH_BUDGET = 56 * 1024
RAM_BUDGET = 7 * 1024
# warn (don't fail) when a PR grows a variant's flash by more than this
GROWTH_WARN = 1024
# software double-precision routines from libgcc: none should be linked, the
# part has no FPU and they cost several KB (see -Wdouble-promotion, Makefile)
SOFT_DOUBLE = re.compile(r"^__(aeabi_(d[a-z0-9]+|[ilu]+2d|f2d)|[a-z]+df[0-9]?)$")
TOP_SYMBOLS = 8


def tool(prefix, name):
    return prefix + name


def sizes(prefix, elf):
    """flash = text + data, ram = data + bss (bss includes heap/stack reserve)"""
    out = subprocess.run(
        [tool(prefix, "size"), "-A", str(elf)], capture_output=True, text=True, check=True
    ).stdout
    sec = {}
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 3 and f[1].isdigit() and f[0].startswith("."):
            sec[f[0]] = int(f[1])
    data = sec.get(".data", 0)
    flash_sections = (".isr_vector", ".text", ".rodata", ".ARM.extab", ".ARM")
    flash_sections += (".preinit_array", ".init_array", ".fini_array")
    flash = sum(sec.get(s, 0) for s in flash_sections) + data
    reserve = sec.get("._user_heap_stack", 0)
    ram = data + sec.get(".bss", 0) + reserve
    return flash, ram, reserve


def symbols(prefix, elf):
    """name -> size for symbols that take flash or RAM. Aliases (several
    names at one address, e.g. __aeabi_dadd / __adddf3) count once, under
    their names joined with " / " """
    out = subprocess.run(
        [tool(prefix, "nm"), "-S", "--size-sort", str(elf)],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    by_addr = {}
    for line in out.splitlines():
        f = line.split()
        if len(f) == 4 and f[2].lower() in "tdbrw":
            names, _ = by_addr.get(f[0], ([], 0))
            by_addr[f[0]] = (names + [f[3]], int(f[1], 16))
    syms = {}
    for names, size in by_addr.values():
        key = " / ".join(sorted(names))
        syms[key] = syms.get(key, 0) + size
    return syms


def names(syms):
    return {n for key in syms for n in key.split(" / ")}


def variants(build):
    return {p.parent.name: p for p in sorted(Path(build).glob("*/dfm17.elf"))}


def kb(n):
    return f"{n / 1024:.1f} KB"


def delta(n):
    return "" if n is None else ("±0" if n == 0 else f"{n:+d} B")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--head", default="build")
    ap.add_argument("--base")
    ap.add_argument("--base-label", help="how to name the base in the report, e.g. main @ abc1234")
    ap.add_argument("--prefix", default=os.environ.get("ARM", "arm-none-eabi-"))
    ap.add_argument("--markdown", help="also write the report to this file (for the PR comment)")
    a = ap.parse_args()

    # the tracker (the default build) first
    head = dict(sorted(variants(a.head).items(), key=lambda kv: (kv[0] != "tracker", kv[0])))
    if not head:
        sys.exit(f"no firmware found under {a.head}/*/dfm17.elf; run make firmware")
    base = variants(a.base) if a.base else {}
    ci = os.environ.get("GITHUB_ACTIONS") == "true"
    failed = False
    md = ["## Firmware size", ""]
    if a.base:
        label = a.base_label or a.base
        md.append(f"Change against {label}. " if base else f"No base build ({label}). ")
    md += [
        (
            f"Budget: flash {kb(FLASH_BUDGET)} of {kb(FLASH_TOTAL)}, "
            f"RAM {kb(RAM_BUDGET)} of {kb(RAM_TOTAL)} "
            "(RAM includes the linker's reserved heap and stack)."
        ),
        "",
        "| Variant | Flash | Change | Of 64 KB | RAM | Change | Of 8 KB | Soft double |",
        "|---|---|---|---|---|---|---|---|",
    ]
    details = []
    shown = {}
    reserves = []
    for name, elf in head.items():
        flash, ram, reserve = sizes(a.prefix, elf)
        syms = symbols(a.prefix, elf)
        reserves.append(reserve)
        doubles = sorted(s for s in names(syms) if SOFT_DOUBLE.match(s))
        dflash = dram = None
        if name in base:
            bflash, bram, _ = sizes(a.prefix, base[name])
            dflash, dram = flash - bflash, ram - bram
        over = []
        if flash > FLASH_BUDGET:
            over.append(f"flash {flash} B over the {FLASH_BUDGET} B budget")
        if ram > RAM_BUDGET:
            over.append(f"RAM {ram} B over the {RAM_BUDGET} B budget")
        if doubles:
            over.append("links software double routines: " + ", ".join(doubles))
        for msg in over:
            failed = True
            print(f"::error title=Firmware size ({name})::{msg}" if ci else f"ERROR {name}: {msg}")
        if dflash is not None and dflash > GROWTH_WARN:
            msg = f"flash grew by {dflash} B (more than {GROWTH_WARN} B)"
            print(
                f"::warning title=Firmware size ({name})::{msg}" if ci else f"WARNING {name}: {msg}"
            )
        mark = " ❌" if over else ""
        md.append(
            f"| {name}{mark} | {flash} B | {delta(dflash)} | {100 * flash / FLASH_TOTAL:.1f}% "
            f"| {ram} B | {delta(dram)} | {100 * ram / RAM_TOTAL:.1f}% "
            f"| {'**yes**' if doubles else 'none'} |"
        )
        if name in base and dflash:
            bsyms = symbols(a.prefix, base[name])
            changes = {
                s: syms.get(s, 0) - bsyms.get(s, 0)
                for s in set(syms) | set(bsyms)
                if syms.get(s, 0) != bsyms.get(s, 0)
            }
            top = sorted(changes.items(), key=lambda kv: -abs(kv[1]))[:TOP_SYMBOLS]
            rows = [f"| `{s}` | {bsyms.get(s, 0)} | {syms.get(s, 0)} | {d:+d} |" for s, d in top]
            same = next((v for v, r in shown.items() if r == rows), None)
            if same:
                details += [f"{name}: same symbol changes as {same}.", ""]
                continue
            shown[name] = rows
            details += [
                f"<details><summary>{name}: biggest symbol changes</summary>",
                "",
                "| Symbol | Base | Head | Change |",
                "|---|---|---|---|",
                *rows,
                "",
                "</details>",
                "",
            ]
    lo, hi = min(reserves), max(reserves)
    span = f"{lo} B" if lo == hi else f"{lo}-{hi} B"
    md += ["", f"Reserved heap + stack (in the RAM figures): {span}.", "", *details]
    report = "\n".join(md)
    print(report)
    if a.markdown:
        with open(a.markdown, "w") as f:
            f.write(report + "\n")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as f:
            f.write(report + "\n")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
