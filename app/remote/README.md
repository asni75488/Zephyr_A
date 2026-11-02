# remote/ – LP-core (ULP) image

This folder will contain the program for the ESP32-C6's low-power RISC-V core
(LP core), built as a separate image via sysbuild.

It is intentionally empty until the LP-core part (part 9). Planned structure
(same as Micro Technic's example and Zephyr 4.4.0's
`samples/boards/espressif/ulp/lp_core/*`):

```
remote/
├── boards/esp32c6_devkitc_lpcore.overlay
├── CMakeLists.txt
├── prj.conf
└── src/main_ULP.c
```

Board target for the LP core in Zephyr 4.4.0: `esp32c6_devkitc/esp32c6/lpcore`.
