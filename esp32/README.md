## Snac2 on ESP32 series (ESP-IDF)

This folder contains an ESP-IDF application that builds Snac2 for ESP32 series.

Any ESP32 chip with atleast 2 MB+ PSRAM and WiFi support can be used.
This means the following chips are supported:
- ESP32
- ESP32-S3
- ESP32-S2
- ESP32-C5
- ESP32-C61

### Storage layout (SD card, two partitions)

- **CONFIG (FAT, RW over USB MSC)**: operator-editable instance configuration files.
- **DATA (FAT, not exposed over USB MSC)**: Snac2 runtime data (users, media, queues, indexes, caches).

The ESP32 app copies selected config files from CONFIG to DATA on boot (and on demand via the USB CDC console), so Snac2 continues to use a single `basedir` on the DATA volume.

### Management interfaces

- **USB CDC (TinyUSB)**: primary management interface (interactive REPL).
- **USB MSC (TinyUSB)**: exposes only the CONFIG partition as a RW mass-storage volume.

Notes:
- MSC requires enabling the TinyUSB MSC interface in ESP-IDF configuration (menuconfig). The `usb_console` component includes TinyUSB MSC callbacks which are compiled only when MSC is enabled.
- Not all functionality is supported yet, for example SMTP mail support is only supported via port 465 (SMTPS) and port 25 (SMTP).

### Host reverse proxy

Federation requires TLS, so terminate TLS on a host PC (Caddy/Nginx) and forward plain HTTP to the ESP32.

