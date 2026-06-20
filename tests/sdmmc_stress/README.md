# SDMMC Stress Test

Minimal ESP-IDF app for testing the ESP32-P4 SD-card path without building the
full firmware.

Default behavior:

- SDMMC slot 0, GPIO45 card power enable.
- UHS-I SDR50, 4-bit, manual phase 2.
- DDR disabled.
- SD IO voltage forced to 3.3 V before card power cycle.
- PSRAM DMA bounce wrapper around `sdmmc_read_sectors` and
  `sdmmc_write_sectors`.
- Repeated mount, write, fsync, read-verify, unmount cycles.
- ESP-Hosted disabled by default for a pure SD-card baseline.

Build and flash:

```sh
idf.py -C tests/sdmmc_stress -B build-sdonly build
idf.py -C tests/sdmmc_stress -B build-sdonly -p /dev/cu.usbmodem1101 flash monitor
```

Build with ESP-Hosted enabled on SDIO slot 1:

```sh
idf.py -C tests/sdmmc_stress \
  -B build-hosted \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.hosted" \
  build
idf.py -C tests/sdmmc_stress -B build-hosted -p /dev/cu.usbmodem1101 flash monitor
```

Recommended production-order ZIP test command:

```sh
idf.py -C tests/sdmmc_stress \
  -B /tmp/sdmmc-stress-prodorder-zip \
  -DSDKCONFIG=/tmp/sdmmc-stress-prodorder-zip.sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.hosted" \
  build
```

If a generated `tests/sdmmc_stress/sdkconfig` exists from a previous run, IDF
will reuse it. For clean A/B tests either remove that generated file or pass an
external sdkconfig path, for example:

```sh
idf.py -C tests/sdmmc_stress -B /tmp/sdmmc-stress-sdonly \
  -DSDKCONFIG=/tmp/sdmmc-stress-sdonly.sdkconfig \
  build

idf.py -C tests/sdmmc_stress -B /tmp/sdmmc-stress-hosted \
  -DSDKCONFIG=/tmp/sdmmc-stress-hosted.sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.hosted" \
  build
```

Useful Kconfig settings are under `TBD SDMMC Stress Test`.
