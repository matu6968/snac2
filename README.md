# snac fork for ESP32 series

**Project no longer actively maintained.**

Ever since [snac 2.90](https://codeberg.org/grunfink/snac2/commit/787d0180bff0655045c3cf42bf8868609c11a6f5) got released the ESP32 port will no longer be updated anymore as this was more of a fun little experiment then a real stable Fediverse server.
The reasons for the sudden stop of maintenance were the fact that under load of servers federating, the ESP32 kept crashing and struggling as i followed more users throughout the Fediverse eventually leading to moving to a more powerful server. 

Whether the project might get picked up again by another developer might be a guess but for now goodbye for the ESP32 port, below is the original readme of the project:

This is a fork of the original snac project to port it to the ESP32 series of chips.

## Requirements

- ESP32 series chip with atleast 2 MB+ PSRAM and WiFi support
- SD card with atleast 2 GB of free space
- WiFi credentials

## Building and installation for ESP32 series

You need to have the ESP-IDF installed and configured.

```sh
# if you haven't already, download the ESP-IDF toolchain
git clone https://github.com/espressif/esp-idf.git --branch release/v5.5 --recurse-submodules esp-idf
# install ESP-IDF dependencies
cd esp-idf
./install.sh
# source the ESP-IDF environment
source ./export.sh
# build the project
cd ../snac2/esp32
idf.py set-target esp32s3 # or esp32s2, esp32c5, esp32c61, esp32 for your chip
idf.py menuconfig
# here you can enter the WiFi credentials under 'Snac2 ESP32' -> 'Enable Wi-Fi station' and 'Wi-Fi SSID' and 'Wi-Fi password'
idf.py build
```
### SD card

By default the SD card will be connected to the ESP32 on GPIO pins 10, 11, 12 and 13. You can change this in `esp32/main/app_main.c`:
```c
// SD card GPIOs
// change these if you have a different SD card pinout
#define SD_CS_PIN 10
#define SD_MOSI_PIN 11
#define SD_MISO_PIN 13
#define SD_SCLK_PIN 12
``` 

Prepare a FAT32 formatted SD card with two partitions:
- CONFIG (16 MB)
- DATA (rest of the space)

You can leave the partitions empty as the ESP32 app will format them upon typing `init` in the REPL.

### REPL

By default the REPL will be available on the USB CDC port and any output of commands will be printed to the USB JTAG port.

The commands are mostly the same as the original snac, but with some additional commands for the ESP32 series.

- `help` will show the available commands.
- `sync_config` will copy the configuration files from the CONFIG partition to the DATA partition.
- `reboot` will restart the ESP32.


## Limitations

- Shared memory is not supported on the ESP32 series, so the build is configured with `WITHOUT_SHM`
- Due to limited amounts of RAM on the ESP32 series (and the slow bandwidth to the SD card), keep the number of users and media to a minimum otherwaise you will see often crashes due to the ESP32 being overwelmed from federating users.
- Due to the ESP32 series not having a powerful CPU to encode images/videos neither a dedicated video encoder (except the ESP32-P4 where it has a H.264 one), media processing is a no-op meaning recieved media gets directly uploaded to the data directory without stripping metadata.

Below is the original README.md file for snac.

------------------------------------------------

# snac

A simple, minimalistic ActivityPub instance

## Features

- Lightweight, minimal dependencies
- Extensive support of ActivityPub operations, e.g. write public notes, follow users, be followed, reply to the notes of others, admire wonderful content (like or boost), write private messages...
- Multiuser
- Mastodon API support, so Mastodon-compatible apps can be used
- Simple but effective web interface
- Easily-accessed MUTE button to silence morons
- Tested interoperability with related software
- No database needed
- Totally JavaScript-free
- No cookies either
- Not much bullshit

## About

This program runs as a daemon (proxied by a TLS-enabled real httpd server) and provides the basic services for a Fediverse / ActivityPub instance (sharing messages and stuff from/to other systems like Mastodon, Pleroma, Friendica, etc.).

This is not the manual; man pages `snac(1)` (user manual), `snac(5)` (formats) and `snac(8)` (administrator manual) are what you are looking for.

`snac` stands for Social Networks Are Crap.

## Building and installation

This program is written in highly portable C. It uses the `__attribute__((__cleanup__))` GNU extension, that is supported at least by the `gcc`, `clang` and `tcc` C compilers. The only external dependencies are `openssl` and `curl`.

On Debian/Ubuntu, you can satisfy these requirements by running

```sh
apt install libssl-dev libcurl4-openssl-dev
```

On OpenBSD you just need to install `curl`:

```sh
pkg_add curl
```

On FreeBSD, to install `curl` just type:

```sh
pkg install curl
```

On NetBSD, to install `curl` just type:

```sh
pkgin install curl
```

The source code is available [here](https://comam.es/what-is-snac).

Run `make` and then `make install` as root. 

If you're compiling on NetBSD, you should use the specific provided Makefile and run `make -f Makefile.NetBSD` and then `make -f Makefile.NetBSD install` as root.

From version 2.27, `snac` includes support for the Mastodon API; if you are not interested on it, you can compile it out by running

```sh
make CFLAGS=-DNO_MASTODON_API
```

If your compilation process complains about undefined references to `shm_open()` and `shm_unlink()` (it happens, for example, on Ubuntu 20.04.6 LTS), run it as:

```sh
make LDFLAGS=-lrt
```

If it still gives compilation errors (because your system does not implement the shared memory functions), you can fix it with

```sh
make CFLAGS=-DWITHOUT_SHM
```

From version 2.68, Linux Landlock sandboxing is included (not supported on Linux kernels older than 5.13.0). It's still a bit experimental, so you must compile it in explicitly with

```sh
make CFLAGS=-DWITH_LINUX_SANDBOX
```

From version 2.73, the language of the web UI can be configured; the `po/` source subdirectory includes a set of translation files, one per language. After initializing your instance, copy whatever language file you want to use to the `lang/` subdirectory of the base directory.

See the administrator manual on how to proceed from here.

## Testing via Docker

A `docker-compose` file is provided for development and testing. To start snac with an nginx HTTPS frontend, run:

```sh
docker-compose build && docker-compose up
```

This will:

- Start snac, storing data in `data/`
- Configure snac to listen on port 8001 with a server name of `localhost` (see `examples/docker-entrypoint.sh`)
- Create a new user `testuser` and print the user's generated password on the console (see `examples/docker-entrypoint.sh`)
- Start nginx to handle HTTPS, using the certificate pair from `nginx-alpine-ssl/nginx-selfsigned.*` (see `examples/nginx-alpine-ssl/entrypoint.sh`)

## Links of Interest

- [Online snac manuals (user, administrator and data formats)](https://comam.es/snac-doc/).
- [How to install snac on OpenBSD without relayd (by @antics@mastodon.nu)](https://chai.guru/pub/openbsd/snac.html).
- [Setting up Snac in OpenBSD (by Yonle)](https://wiki.ircnow.org/index.php?n=Openbsd.Snac).
- [How to run your own social network with snac (by Giacomo Tesio)](https://encrypted.tesio.it/2024/12/18/how-to-run-your-own-social-network.html). Includes information on how to run snac as a CGI.
- [Improving snac Performance with Nginx Proxy Cache (by Stefano Marinelli)](https://it-notes.dragas.net/2025/01/29/improving-snac-performance-with-nginx-proxy-cache/).
- [Caching Snac Proxied Media With Nginx (by Stefano Marinelli)](https://it-notes.dragas.net/2025/02/08/caching-snac-proxied-media-with-nginx/).
- [My snac config (activitypub instance) with Caddy (by ffuentes)](https://ffuentes.sdf.org/communication/2025/08/23/my-snac-config-activitypub-instance-with-caddy.html).
- [A bit of lore about Susie, snac's default avatar](https://comam.es/snac/grunfink/p/1754553922.333170).

## Incredibly awesome CSS themes for snac

- [A compilation of themes for snac (by Ворон)](https://codeberg.org/voron/snac-style).
- [A cool, elegant theme (by Haijo7)](https://codeberg.org/Haijo7/snac-custom-css).
- [A terminal-like theme (by Tetra)](https://codeberg.org/ERROR404NULLNOTFOUND/snac-terminal-theme).

## License

See the LICENSE file for details.

## Author

grunfink [@grunfink@comam.es](https://comam.es/snac/grunfink) with the help of others.

Buy grunfink a coffee: https://ko-fi.com/grunfink/

Contribute via LiberaPay: https://liberapay.com/grunfink/
