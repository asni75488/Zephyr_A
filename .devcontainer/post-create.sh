#!/usr/bin/env bash
# Initialise the west workspace inside the devcontainer (idempotent).
set -euo pipefail

WS=/workdir/zp
cd "$WS"

# The named volume is created as root; give the container user ownership.
sudo chown "$(id -u):$(id -g)" "$WS" || true

if [ ! -d "$WS/.west" ]; then
    west init -l "$WS/zephyr_a"
fi
west update --narrow -o=--depth=1
west config zephyr.base-prefer configfile
west zephyr-export
pip install --user $(west packages pip)
west blobs fetch hal_espressif --auto-accept

echo "Devcontainer ready. Build with:"
echo "  west build --sysbuild -p auto -b esp32c6_devkitc/esp32c6/hpcore -S espressif-flash-4M app -d build"
