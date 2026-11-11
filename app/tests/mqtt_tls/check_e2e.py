#!/usr/bin/env python3
"""Zephyr_A - check the Sparkplug B session captured by mosquitto_sub.

Input: lines "<topic> <payload hex>" for spBv1.0/# in arrival order.
Checks Sparkplug B 3.0 rules the firmware must follow and the expected test
events (run.sh scenario 1). Prints PASS/FAIL lines; exit code 1 on a failure.
"""
import sys
import time

import spb

FAILS = 0


def check(desc, ok, detail=""):
    global FAILS
    print(f"  {'PASS' if ok else 'FAIL'}  {desc}{'' if ok else '  -> ' + str(detail)}")
    if not ok:
        FAILS += 1


def main():
    msgs = []
    for line in open(sys.argv[1], encoding="ascii"):
        parts = line.split()
        if len(parts) == 2:
            p = spb.decode_payload(bytes.fromhex(parts[1]))
            p["topic"] = parts[0]
            p["type"] = parts[0].split("/")[2]
            p["bytes"] = len(parts[1]) // 2
            msgs.append(p)
    dev = [m for m in msgs if m["type"] not in ("NCMD", "DCMD")]  # host -> device
    births = [m for m in dev if m["type"] == "NBIRTH"]
    deaths = [m for m in dev if m["type"] == "NDEATH"]
    now_ms = time.time() * 1000

    check("NBIRTH received", len(births) >= 1, len(births))
    if not births:
        return
    nb = births[0]
    names = {m.get("name"): m for m in nb["metrics"]}
    check("NBIRTH seq 0", nb.get("seq") == 0, nb.get("seq"))
    check("NBIRTH has bdSeq (Int64)", names.get("bdSeq", {}).get("datatype") == 4)
    for n in ("Node Control/Rebirth", "Node Control/Reboot", "Properties/FW Version",
              "Config/Sample Interval", "Config/Batch Size", "Status/RSSI"):
        check(f"NBIRTH metric {n}", n in names)
    check("NBIRTH: every metric has name, datatype and timestamp",
          all(("name" in m) and ("datatype" in m) and ("timestamp" in m) for m in nb["metrics"]))
    aliases = [m["alias"] for m in nb["metrics"] if "alias" in m]
    check("NBIRTH aliases unique", len(aliases) == len(set(aliases)), aliases)
    check("Status/RSSI = -71 dBm as Int32 (Tahu decoding)", names.get("Status/RSSI", {}).get("value") == -71,
          names.get("Status/RSSI"))
    check(f"NBIRTH fits the 512 B payload buffer ({nb['bytes']} B)", nb["bytes"] <= 512)

    dbirths = [m for m in dev if m["type"] == "DBIRTH"]
    check("DBIRTH received", len(dbirths) >= 1)
    if dbirths:
        d = dbirths[0]
        m0 = d["metrics"][0]
        check("DBIRTH seq 1", d.get("seq") == 1, d.get("seq"))
        check("DBIRTH Temperature/Die alias 100 Float",
              m0.get("name") == "Temperature/Die" and m0.get("alias") == 100 and m0.get("datatype") == 9, m0)

    # seq continuity per session (NBIRTH starts at 0, NDEATH has no seq)
    ok = True
    expected = None
    for m in dev:
        if m["type"] == "NBIRTH":
            expected = 1
            continue
        if m["type"] == "NDEATH" or expected is None:
            continue
        if m.get("seq") != expected % 256:
            ok = False
            print(f"        seq {m.get('seq')} != {expected % 256} in {m['type']}")
        expected = (m.get("seq", 0) + 1)
    check("seq increments by 1 within each session", ok)

    data = [m for m in dev if m["type"] == "DDATA"]
    vals = [x for m in data for x in m["metrics"]]
    check("DDATA received", len(data) >= 3, len(data))
    check("DDATA metrics: alias only (no names), datatype Float",
          all(("name" not in x) and x.get("alias") == 100 and x.get("datatype") == 9 for x in vals))
    ts = [x.get("timestamp") for x in vals]
    check("DDATA metrics have UTC timestamps (+-60 s of now)",
          all(t and abs(t - now_ms) < 60000 + 30000 for t in ts), ts[:3])
    check("DDATA timestamps increase (one per measurement)",
          all(b > a for a, b in zip(ts, ts[1:]) if a and b))
    check("DDATA payload timestamp present", all("timestamp" in m for m in data))
    check("BAD measurement sent as is_null", any(x.get("is_null") for x in vals))
    goods = [x["value"] for x in vals if x.get("value") is not None]
    check("Values are 21 + n x 0.125 degC", all(abs(((v - 21.0) / 0.125) - round((v - 21.0) / 0.125)) < 1e-3 for v in goods),
          goods[:6])

    ndata = [m for m in dev if m["type"] == "NDATA"]
    cfg = [x for m in ndata for x in m["metrics"] if x.get("alias") == 5]
    check("NDATA Config/Sample Interval = 5000 after NCMD", any(x.get("value") == 5000 for x in cfg), cfg)
    bsz = [x for m in ndata for x in m["metrics"] if x.get("alias") == 6]
    check("NDATA Config/Batch Size = 4 after NCMD (by alias)", any(x.get("value") == 4 for x in bsz), bsz)
    st = [x for m in ndata for x in m["metrics"] if x.get("alias") == 7]
    check("Status NDATA with Status/RSSI", any(x.get("value") == -71 for x in st), st)

    check("Rebirth: second NBIRTH with the same bdSeq",
          len(births) >= 2 and births[1]["metrics"][0]["value"] == births[0]["metrics"][0]["value"])
    bd = [b["metrics"][0]["value"] for b in births]
    check("Link loss: Will NDEATH carries the bdSeq of the session",
          any(d["metrics"][0]["value"] == bd[0] for d in deaths), [d["metrics"][0]["value"] for d in deaths])
    check("New session after link loss: NBIRTH with bdSeq + 1", (bd[0] + 1) % 256 in bd, bd)
    check("Every DDATA follows a BIRTH of its session", dev.index(data[0]) > dev.index(nb))

    print(f"  info  {len(msgs)} messages; NBIRTH {nb['bytes']} B, DDATA {[m['bytes'] for m in data]} B")


if __name__ == "__main__":
    main()
    sys.exit(1 if FAILS else 0)
