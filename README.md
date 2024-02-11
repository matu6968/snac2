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

This program is written in highly portable C. The only external dependencies are `openssl` and `curl`.

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

* [Online snac manuals (user, administrator and data formats)](https://comam.es/snac-doc/).
* [How to run your own ActivityPub server on OpenBSD via snac (by Jordan Reger)](https://man.sr.ht/~jordanreger/activitypub-server-on-openbsd/).
* [How to install & run your own ActivityPub server on FreeBSD using snac, nginx, lets'encrypt  (by gyptazy)](https://gyptazy.ch/blog/install-snac2-on-freebsd-an-activitypub-instance-for-the-fediverse/).

## Incredibly awesome CSS themes for snac

* [A cool, elegant theme (by Haijo7)](https://codeberg.org/Haijo7/snac-custom-css).
* [A light, lean theme (by Ворон)](https://codeberg.org/voron/snac-style).
* [A terminal-like theme (by Tetra)](https://codeberg.org/ERROR404NULLNOTFOUND/snac-terminal-theme).

## License

See the LICENSE file for details.

## Author

grunfink [@grunfink@comam.es](https://comam.es/snac/grunfink) with the help of others.

Buy grunfink a coffee: https://ko-fi.com/grunfink
