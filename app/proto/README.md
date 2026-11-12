# app/proto – Sparkplug B protobuf definition

`sparkplug_b.proto` is the unmodified file from Eclipse Tahu
(<https://github.com/eclipse-tahu/tahu>, `sparkplug_b/sparkplug_b.proto`,
commit `5736e40`, EPL-2.0). `sparkplug_b.options` tells nanopb how to map it
to small static C structures (see the comments in the file).

The generated C code is **committed** in `app/src/proto/` so a normal build
needs neither `protoc` nor Python protobuf packages on Windows. Regenerate
only when the `.proto` or `.options` file changes:

```powershell
# needs protoc (https://github.com/protocolbuffers/protobuf/releases) on PATH
# and the Python package "protobuf" (pip install protobuf) in the venv
python scripts\gen_sparkplug_pb.py
```

The generator is taken from the nanopb module of the west workspace
(`modules/lib/nanopb`, nanopb 1.0.0-dev as pinned by Zephyr v4.4.0), so generated code
and runtime always match.
