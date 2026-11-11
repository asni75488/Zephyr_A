#!/usr/bin/env python3
"""Zephyr_A - decode the self-test Sparkplug B NDATA captured by mosquitto_sub.

Input: lines "<topic> <payload hex>" (mosquitto_sub -v -F '%t %x').
Output: one line per NDATA, e.g. "seq=0 Test/Uptime=0 Test/RSSI=-71.0 ...".
A minimal protobuf reader (no dependencies) for the fields used by
app/src/mqtt_selftest.c; independent of the encoder, so it checks it.
"""
import struct
import sys

DATATYPES = {8: "UInt64", 9: "Float"}


def varint(buf, i):
    shift = value = 0
    while True:
        b = buf[i]
        i += 1
        value |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            return value, i


def fields(buf):
    i = 0
    while i < len(buf):
        key, i = varint(buf, i)
        num, wt = key >> 3, key & 7
        if wt == 0:
            val, i = varint(buf, i)
        elif wt == 2:
            n, i = varint(buf, i)
            val = buf[i:i + n]
            i += n
        elif wt == 5:
            val = buf[i:i + 4]
            i += 4
        elif wt == 1:
            val = buf[i:i + 8]
            i += 8
        else:
            raise ValueError(f"wire type {wt}")
        yield num, wt, val


def decode(payload):
    out = []
    seq = None
    for num, _wt, val in fields(payload):
        if num == 2:
            name, dtype, value = None, None, None
            for mnum, _mwt, mval in fields(val):
                if mnum == 1:
                    name = mval.decode()
                elif mnum == 4:
                    dtype = mval
                elif mnum == 11:
                    value = mval
                elif mnum == 12:
                    value = round(struct.unpack("<f", mval)[0], 3)
            out.append(f"{name}={value} ({DATATYPES.get(dtype, dtype)})")
        elif num == 3:
            seq = val
    return f"seq={seq} " + " ".join(out)


def main():
    for line in open(sys.argv[1], encoding="ascii"):
        parts = line.split()
        if len(parts) == 2 and "/NDATA/" in parts[0]:
            print(parts[0], decode(bytes.fromhex(parts[1])))


if __name__ == "__main__":
    main()
