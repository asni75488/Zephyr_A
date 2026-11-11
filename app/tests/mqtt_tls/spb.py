#!/usr/bin/env python3
"""Zephyr_A - minimal Sparkplug B protobuf codec for the tests.

Independent of nanopb and protoc: reads/writes the protobuf wire format of
the fields the firmware uses (Eclipse Tahu sparkplug_b.proto):
  Payload: 1 timestamp, 2 metrics (repeated), 3 seq
  Metric:  1 name, 2 alias, 3 timestamp, 4 datatype, 5 is_historical, 7 is_null,
           10 int_value, 11 long_value, 12 float_value, 13 double_value,
           14 boolean_value, 15 string_value

Command line: spb.py decode <file with "topic hex" lines>  -> JSON lines
              spb.py ncmd <name>=<type>:<value> ...         -> payload bytes on stdout
"""
import json
import struct
import sys
import time

DT = {"Int32": 3, "Int64": 4, "Float": 9, "Boolean": 11, "String": 12}
DT_NAME = {v: k for k, v in DT.items()}
DT_NAME.update({1: "Int8", 2: "Int16", 5: "UInt8", 6: "UInt16", 7: "UInt32", 8: "UInt64",
                10: "Double", 13: "DateTime", 14: "Text"})


def _varint(buf, i):
    shift = value = 0
    while True:
        b = buf[i]
        i += 1
        value |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            return value, i


def _fields(buf):
    i = 0
    while i < len(buf):
        key, i = _varint(buf, i)
        num, wt = key >> 3, key & 7
        if wt == 0:
            val, i = _varint(buf, i)
        elif wt == 2:
            n, i = _varint(buf, i)
            val = bytes(buf[i:i + n])
            i += n
        elif wt == 5:
            val = bytes(buf[i:i + 4])
            i += 4
        elif wt == 1:
            val = bytes(buf[i:i + 8])
            i += 8
        else:
            raise ValueError(f"wire type {wt}")
        yield num, wt, val


def decode_metric(buf):
    m = {}
    for num, _wt, val in _fields(buf):
        if num == 1:
            m["name"] = val.decode()
        elif num == 2:
            m["alias"] = val
        elif num == 3:
            m["timestamp"] = val
        elif num == 4:
            m["datatype"] = val
        elif num == 5:
            m["is_historical"] = bool(val)
        elif num == 7:
            m["is_null"] = bool(val)
        elif num == 10:
            m["int_value"] = val
        elif num == 11:
            m["long_value"] = val
        elif num == 12:
            m["float_value"] = round(struct.unpack("<f", val)[0], 4)
        elif num == 13:
            m["double_value"] = struct.unpack("<d", val)[0]
        elif num == 14:
            m["boolean_value"] = bool(val)
        elif num == 15:
            m["string_value"] = val.decode()
        else:
            m.setdefault("unknown_fields", []).append(num)
    m["value"] = tahu_value(m)
    return m


def tahu_value(m):
    """The value as Eclipse Tahu's JS decoder (getValue) would return it."""
    if m.get("is_null"):
        return None
    dt = m.get("datatype")
    if dt in (1, 2, 3):
        return struct.unpack("<i", struct.pack("<I", m.get("int_value", 0)))[0]
    if dt in (5, 6):
        return m.get("int_value")
    if dt in (4,):
        return struct.unpack("<q", struct.pack("<Q", m.get("long_value", 0)))[0]
    if dt in (7, 8, 13):
        return m.get("long_value")
    if dt == 9:
        return m.get("float_value")
    if dt == 10:
        return m.get("double_value")
    if dt == 11:
        return m.get("boolean_value")
    if dt in (12, 14):
        return m.get("string_value")
    return None  # Tahu returns null without a known datatype


def decode_payload(buf):
    p = {"metrics": []}
    for num, _wt, val in _fields(buf):
        if num == 1:
            p["timestamp"] = val
        elif num == 2:
            p["metrics"].append(decode_metric(val))
        elif num == 3:
            p["seq"] = val
    return p


def _put_varint(out, v):
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            return


def _key(out, num, wt):
    _put_varint(out, (num << 3) | wt)


def encode_metric(name, dtype, value, alias=None):
    m = bytearray()
    if name is not None:
        _key(m, 1, 2)
        nb = name.encode()
        _put_varint(m, len(nb))
        m += nb
    if alias is not None:
        _key(m, 2, 0)
        _put_varint(m, alias)
    _key(m, 4, 0)
    _put_varint(m, DT[dtype])
    if dtype == "Boolean":
        _key(m, 14, 0)
        _put_varint(m, 1 if value in ("1", "true", True) else 0)
    elif dtype == "Int64":
        _key(m, 11, 0)
        _put_varint(m, int(value) & 0xFFFFFFFFFFFFFFFF)
    elif dtype == "Int32":
        _key(m, 10, 0)
        _put_varint(m, int(value) & 0xFFFFFFFF)
    elif dtype == "Float":
        _key(m, 12, 5)
        m += struct.pack("<f", float(value))
    elif dtype == "String":
        _key(m, 15, 2)
        vb = str(value).encode()
        _put_varint(m, len(vb))
        m += vb
    return bytes(m)


def encode_ncmd(metrics):
    """metrics: list of (name or None, type, value, alias or None)"""
    out = bytearray()
    _key(out, 1, 0)
    _put_varint(out, int(time.time() * 1000))
    for name, dtype, value, alias in metrics:
        mb = encode_metric(name, dtype, value, alias)
        _key(out, 2, 2)
        _put_varint(out, len(mb))
        out += mb
    return bytes(out)


def main():
    if sys.argv[1] == "decode":
        for line in open(sys.argv[2], encoding="ascii"):
            parts = line.split()
            if len(parts) != 2:
                continue
            topic, hexdata = parts
            raw = bytes.fromhex(hexdata)
            rec = {"topic": topic, "type": topic.split("/")[2], "bytes": len(raw)}
            rec.update(decode_payload(raw))
            print(json.dumps(rec))
    elif sys.argv[1] == "ncmd":
        metrics = []
        for arg in sys.argv[2:]:
            key, tv = arg.split("=", 1)
            dtype, value = tv.split(":", 1)
            if key.startswith("#"):
                metrics.append((None, dtype, value, int(key[1:])))
            else:
                metrics.append((key, dtype, value, None))
        sys.stdout.buffer.write(encode_ncmd(metrics))
    else:
        sys.exit("usage: spb.py decode <file> | spb.py ncmd name=type:value ...")


if __name__ == "__main__":
    main()
