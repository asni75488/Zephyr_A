#!/usr/bin/env python3
"""Zephyr_A - regenerate the nanopb code for Sparkplug B.

Runs the nanopb generator of the west workspace (modules/lib/nanopb) on
app/proto/sparkplug_b.proto with app/proto/sparkplug_b.options and writes
app/src/proto/sparkplug_b.pb.c/.h. The output is committed, so this is only
needed when the .proto or .options file changes (app/proto/README.md).

Requirements: protoc on PATH, Python package "protobuf".
Works on Windows (PowerShell: python scripts\\gen_sparkplug_pb.py) and Linux.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WORKSPACE = REPO.parent
PROTO_DIR = REPO / "app" / "proto"
OUT_DIR = REPO / "app" / "src" / "proto"
GENERATOR = WORKSPACE / "modules" / "lib" / "nanopb" / "generator" / "nanopb_generator.py"


def main():
    if not GENERATOR.exists():
        sys.exit(f"nanopb generator not found: {GENERATOR} (run 'west update' first)")
    if shutil.which("protoc") is None:
        sys.exit("protoc not found on PATH")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    cmd = [
        sys.executable, str(GENERATOR),
        "--proto-path", str(PROTO_DIR),
        "--options-path", str(PROTO_DIR),
        "--output-dir", str(OUT_DIR),
        # Plain include of pb.h (the nanopb module adds it to the include path)
        "--library-include-format", '#include <%s>',
        str(PROTO_DIR / "sparkplug_b.proto"),
    ]
    print(" ".join(cmd))
    subprocess.run(cmd, check=True, env=os.environ)
    for f in sorted(OUT_DIR.glob("sparkplug_b.pb.*")):
        print(f"wrote {f.relative_to(REPO)} ({f.stat().st_size} B)")


if __name__ == "__main__":
    main()
