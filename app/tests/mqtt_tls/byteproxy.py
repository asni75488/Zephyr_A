#!/usr/bin/env python3
"""Zephyr_A - TCP proxy that counts the bytes on the wire (part 9 data test).

Usage: byteproxy.py <listen port> <target port> <report file>

Every connection is forwarded to 127.0.0.1:<target port>. When a connection
closes, one line is appended to the report file:
    conn <n> up <bytes device->broker> down <bytes broker->device> seconds <t>
The counts are TCP payload bytes (TLS records); TCP/IP/PPP headers come on
top (docs/09-strom-data.md, section 5).
"""
import socket
import sys
import threading
import time

LISTEN, TARGET, REPORT = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
lock = threading.Lock()
count = 0


def pump(src, dst, box, key):
    try:
        while True:
            data = src.recv(65536)
            if not data:
                break
            box[key] += len(data)
            dst.sendall(data)
    except OSError:
        pass
    finally:
        for s in (src, dst):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


def handle(client, n):
    box = {"up": 0, "down": 0}
    t0 = time.time()
    server = socket.create_connection(("127.0.0.1", TARGET))
    a = threading.Thread(target=pump, args=(client, server, box, "up"))
    b = threading.Thread(target=pump, args=(server, client, box, "down"))
    a.start()
    b.start()
    a.join()
    b.join()
    with lock, open(REPORT, "a", encoding="ascii") as f:
        f.write(f"conn {n} up {box['up']} down {box['down']} seconds {time.time() - t0:.1f}\n")


def main():
    global count
    ls = socket.socket()
    ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(("0.0.0.0", LISTEN))
    ls.listen(4)
    while True:
        c, _ = ls.accept()
        count += 1
        threading.Thread(target=handle, args=(c, count), daemon=True).start()


if __name__ == "__main__":
    main()
