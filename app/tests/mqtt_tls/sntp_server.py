#!/usr/bin/env python3
"""Zephyr_A - tiny SNTP server for the native_sim test (RFC 4330, mode 4).

Usage: sntp_server.py <port> [<offset seconds>]
Answers every request with the host time (+ offset). An offset of
-1000000000 makes it answer with a 1990s time, which the firmware must reject.
"""
import socket
import struct
import sys
import time

NTP_EPOCH_OFFSET = 2208988800  # 1900 -> 1970


def ntp_ts(t):
    secs = int(t)
    frac = int((t - secs) * (1 << 32))
    return struct.pack("!II", secs + NTP_EPOCH_OFFSET, frac)


def main():
    port = int(sys.argv[1])
    offset = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", port))
    while True:
        data, addr = s.recvfrom(512)
        if len(data) < 48:
            continue
        rx = time.time() + offset
        originate = data[40:48]  # client's transmit timestamp
        # LI=0, VN=4, mode=4 (server), stratum 2, poll, precision
        hdr = struct.pack("!BBbb", (0 << 6) | (4 << 3) | 4, 2, data[2], -20)
        root = struct.pack("!II", 0, 0) + b"LOCL"
        reply = hdr + root + ntp_ts(rx) + originate + ntp_ts(rx) + ntp_ts(time.time() + offset)
        s.sendto(reply, addr)


if __name__ == "__main__":
    main()
