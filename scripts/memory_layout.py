#!/usr/bin/env python3
"""Zephyr_A - memory layout report from a sysbuild build directory (part 11).

    python scripts/memory_layout.py build            > layout.md
    python scripts/memory_layout.py build --check    exit 1 if a limit is exceeded

Reads only build outputs, so the numbers can always be reproduced:
  app/zephyr/zephyr.dts        flash partitions, LP SRAM regions
  app/zephyr/zephyr.map        linker memory regions (HP SRAM size)
  app/zephyr/zephyr.elf        sections and symbols (pyelftools)
  zephyr_a_lpcore/zephyr/      LP-core image
  mcuboot/zephyr/zephyr.bin    bootloader size
  app/zephyr/zephyr.signed.bin signed application image
Requires pyelftools (installed with Zephyr's requirements).

--check limits (chosen in docs/11-afslutning.md): application image <= 50 % of
its slot (room for OTA growth), HP SRAM <= 75 %, LP image <= 50 % of ulp_ram,
largest batch DDATA payload stays below the MQTT buffer (checked in tests).
"""

import argparse
import re
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection

LIMITS = {"slot": 0.50, "sram": 0.75, "lp": 0.50}


def kib(n):
    return f"{n / 1024:.1f} KiB"


def pct(a, b):
    return f"{100.0 * a / b:.1f} %" if b else "-"


def dts_nodes(dts_text, parent_pattern):
    """(label, name, unit address, reg base, reg size) of the children of a node."""
    m = re.search(parent_pattern, dts_text)
    if not m:
        return []
    start = dts_text.index("{", m.end() - 1)
    depth, i = 0, start
    while True:  # find the matching closing brace of the parent
        if dts_text[i] == "{":
            depth += 1
        elif dts_text[i] == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    body = dts_text[start + 1:i]
    out = []
    for c in re.finditer(r"(?:(\w+):\s*)?([\w,-]+)@([0-9a-f]+)\s*\{(.*?)\};", body, re.S):
        reg = re.search(r"reg\s*=\s*<\s*(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s*>", c.group(4))
        lab = re.search(r'label\s*=\s*"([^"]+)"', c.group(4))
        if reg:
            out.append({"node": c.group(1) or c.group(2), "label": lab.group(1) if lab else "",
                        "base": int(reg.group(1), 16), "size": int(reg.group(2), 16)})
    return sorted(out, key=lambda n: n["base"])


def map_regions(map_text):
    regs = {}
    block = map_text.split("Memory Configuration", 1)[1].split("Linker script", 1)[0]
    for m in re.finditer(r"^(\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)", block, re.M):
        regs[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    return regs


def elf_info(path):
    with open(path, "rb") as f:
        elf = ELFFile(f)
        secs = [(s.name, s["sh_addr"], s["sh_size"], s["sh_type"])
                for s in elf.iter_sections() if s["sh_flags"] & 0x2 and s["sh_size"] > 0]
        syms = []
        for s in elf.iter_sections():
            if isinstance(s, SymbolTableSection):
                for y in s.iter_symbols():
                    if y["st_info"]["type"] == "STT_OBJECT" and y["st_size"] > 0:
                        syms.append((y.name, y["st_value"], y["st_size"]))
    return secs, syms


def in_region(addr, region):
    return region[0] <= addr < region[0] + region[1]


def classify(name):
    n = name.lower()
    if "stack" in n:
        return "tråd-stack"
    if "heap" in n or "mempool" in n or "kheap" in n:
        return "heap"
    if any(k in n for k in ("msgq", "fifo_buf", "_q_buf", "queue", "ring", "slab")):
        return "kø/buffer"
    if any(k in n for k in ("simcom", "modem", "cmux", "cellular", "ppp")):
        return "modem"
    if any(k in n for k in ("net_", "pkt", "buf_pool", "_bufs", "tls_", "dns", "sock")):
        return "netværk/TLS"
    return "øvrigt"


def report(build, check):
    app = build / "app" / "zephyr"
    dts = (app / "zephyr.dts").read_text()
    regions = map_regions((app / "zephyr.map").read_text())
    secs, syms = elf_info(app / "zephyr.elf")
    signed = (app / "zephyr.signed.bin").stat().st_size
    boot = (build / "mcuboot" / "zephyr" / "zephyr.bin").stat().st_size
    lp_bin_f = build / "zephyr_a_lpcore" / "zephyr" / "zephyr.bin"
    lp_bin = lp_bin_f.stat().st_size if lp_bin_f.exists() else 0
    problems = []
    out = []
    w = out.append

    # --- flash -------------------------------------------------------------
    parts = dts_nodes(dts, r"partitions\s*\{")
    used = {"boot_partition": boot, "slot0_partition": signed,
            "slot0_lpcore_partition": lp_bin}
    what = {"boot_partition": "MCUboot (verificerer slot0 ved hver boot, Del 8)",
            "sys_partition": "ESP-IDF systemdata (PHY-kalibrering)",
            "slot0_partition": "Applikation, signeret (MCUboot-header + ECDSA-TLV)",
            "slot1_partition": "Opdateringsslot (OTA, N-02 - ikke brugt endnu)",
            "slot0_lpcore_partition": "LP-kerne-image (Del 9, SHA-256 tjekket af app)",
            "slot1_lpcore_partition": "LP-image til OTA (ikke brugt)",
            "storage_partition": "Lager (NVS/settings - ikke brugt endnu)",
            "scratch_partition": "MCUboot scratch (swap ved OTA)",
            "coredump_partition": "Coredump (ikke slået til)"}
    w("### Flash (4 MB, `partitions_0x0_default_4M.dtsi`)\n")
    w("| Adresse | Størrelse | Partition | Indhold | Brugt |")
    w("|---|---|---|---|---|")
    for p in parts:
        u = used.get(p["node"])
        w(f"| `0x{p['base']:06X}` | {kib(p['size'])} | `{p['label']}` | {what.get(p['node'], '')} | "
          + (f"{kib(u)} ({pct(u, p['size'])})" if u is not None else "–") + " |")
        if p["node"] == "slot0_partition" and signed > LIMITS["slot"] * p["size"]:
            problems.append(f"app image {pct(signed, p['size'])} of slot0 > {LIMITS['slot']:.0%}")
    w("")
    total = 4 * 1024 * 1024
    w("```")
    for p in parts:
        bar = max(1, round(60 * p["size"] / total))
        u = used.get(p["node"])
        fill = round(bar * u / p["size"]) if u else 0
        w(f"0x{p['base']:06X} |{'#' * fill}{'.' * (bar - fill)}| {p['label']}")
    w("0x400000  (# = brugt, . = fri; bredde ~ størrelse)")
    w("```\n")

    # --- HP SRAM -----------------------------------------------------------
    sram = regions["sram0_0_seg"]
    ram_secs = [s for s in secs if in_region(s[1], sram)]
    ram_used = sum(s[2] for s in ram_secs)
    w(f"### HP-SRAM (`sram0_0_seg`, {kib(sram[1])} til applikationen)\n")
    w(f"Brugt: **{kib(ram_used)} ({pct(ram_used, sram[1])})**. Fri: {kib(sram[1] - ram_used)}.\n")
    w("| Sektion | Størrelse | Hvad |")
    w("|---|---|---|")
    meaning = {".iram0.text": "kode der skal køre fra RAM (ISR, flash-driver)",
               ".dram0.data": "initialiserede variabler", ".dram0.bss": "nulstillede variabler (stacks, heaps, køer)",
               ".dram0.noinit": "ikke-initialiseret (bl.a. stacks)", "noinit": "ikke-initialiseret (bl.a. stacks)",
               ".iram0.vectors": "interrupt-vektorer", ".iram0.data": "data i IRAM", ".iram0.bss": "bss i IRAM"}
    for name, _, size, _ in sorted(ram_secs, key=lambda s: -s[2]):
        if size >= 512:
            w(f"| `{name}` | {kib(size)} | {meaning.get(name, '')} |")
    small = sum(s[2] for s in ram_secs if s[2] < 512)
    w(f"| (øvrige små) | {kib(small)} | |")
    w("")
    if ram_used > LIMITS["sram"] * sram[1]:
        problems.append(f"HP SRAM {pct(ram_used, sram[1])} > {LIMITS['sram']:.0%}")

    ram_syms = sorted((s for s in syms if in_region(s[1], sram)), key=lambda s: -s[2])
    groups = {}
    for n, _, size in ram_syms:
        groups.setdefault(classify(n), 0)
        groups[classify(n)] += size
    w("**Største RAM-objekter** (symboler ≥ 2 KiB):\n")
    w("| Symbol | Størrelse | Type |")
    w("|---|---|---|")
    for n, _, size in ram_syms:
        if size >= 2048:
            w(f"| `{n}` | {kib(size)} | {classify(n)} |")
    w("")
    w("| Type (alle symboler) | Sum |")
    w("|---|---|")
    for g, size in sorted(groups.items(), key=lambda x: -x[1]):
        w(f"| {g} | {kib(size)} |")
    w("")

    # --- code in flash -------------------------------------------------------
    irom, drom = regions["irom0_0_seg"], regions["drom0_0_seg"]
    code = sum(s[2] for s in secs if in_region(s[1], irom))
    rodata = sum(s[2] for s in secs if in_region(s[1], drom))
    w("### Kode og konstanter (kører/læses fra flash via cache)\n")
    w("| Område | Størrelse |")
    w("|---|---|")
    w(f"| `irom` – kode | {kib(code)} |")
    w(f"| `drom` – konstanter, strenge, CA-certifikater (DER, ikke kopieret til RAM: `TLS_CERT_NOCOPY`) | {kib(rodata)} |")
    w(f"| Signeret image i alt | {kib(signed)} |")
    w("")

    # --- LP SRAM -------------------------------------------------------------
    lp_nodes = dts_nodes(dts, r"sramlp:\s*memory@50000000\s*\{")
    lp_meaning = {"ulp_ram": "LP-kerne-image (kode, data, stack)",
                  "app_lp_shm": "Zephyr_A: delt HP/LP (`lp_shared.h`, Del 9/10)",
                  "ulp_shm": "ESP-IDF LP-kernens konfiguration (timer)",
                  "lp_rtc": "HP `RTC_DATA_ATTR`-data",
                  "retainedmem": "Zephyr retained memory (bevares over reset)",
                  "ipc_shm": "IPC-mailbox (bruges ikke)", "mbox0": "IPC-mailbox (bruges ikke)"}
    w("### LP-SRAM (16 KiB @ `0x50000000`, altid tændt)\n")
    w("| Adresse | Størrelse | Område | Indhold |")
    w("|---|---|---|---|")
    for n in lp_nodes:
        w(f"| `0x{0x50000000 + n['base']:08X}` | {n['size']} B | `{n['node']}` | {lp_meaning.get(n['node'], '')} |")
    w("")
    lp_elf = build / "zephyr_a_lpcore" / "zephyr" / "zephyr.elf"
    ulp = next((n for n in lp_nodes if n["node"] == "ulp_ram"), None)
    if lp_elf.exists() and ulp:
        lsecs, _ = elf_info(lp_elf)
        lp_used = sum(s[2] for s in lsecs if 0x50000000 <= s[1] < 0x50000000 + ulp["size"])
        w(f"LP-image: {lp_bin} B binær, {lp_used} B i `ulp_ram` inkl. stack "
          f"({pct(lp_used, ulp['size'])} af {ulp['size']} B).\n")
        if lp_used > LIMITS["lp"] * ulp["size"]:
            problems.append(f"LP image {pct(lp_used, ulp['size'])} of ulp_ram > {LIMITS['lp']:.0%}")

    print("\n".join(out))
    if check:
        if problems:
            print("\nLIMITS EXCEEDED:\n  " + "\n  ".join(problems), file=sys.stderr)
            return 1
        print("All memory limits OK", file=sys.stderr)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build", type=Path, help="sysbuild build directory (e.g. build)")
    ap.add_argument("--check", action="store_true", help="exit 1 if a limit is exceeded")
    a = ap.parse_args()
    sys.exit(report(a.build, a.check))


if __name__ == "__main__":
    main()
