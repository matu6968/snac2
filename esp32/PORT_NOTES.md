## Dependency inventory (ESP32-S3 port)

This file documents the seams used to port Snac2 to ESP32-S3 (ESP-IDF), without rewriting core logic.

### Outbound HTTP

- **Seam**: `xs_http_request()` / `xs_smtp_request()` in `xs_curl.h`
- **Used by**:
  - `webfinger.c`, `activitypub.c`, `http.c`, `rss.c`, `utils.c`, `html.c`, `xs_webmention.h`
- **ESP32 plan**:
  - Replace libcurl implementation with `esp_http_client` (mbedTLS under the hood).
  - Keep the same function signature so call sites remain unchanged.
  - `xs_smtp_request()` is not required for federation MVP; implement as `ENOTSUP` initially.

### Crypto / signatures

- **Seam**: `xs_openssl.h`
  - `xs_md5_hex()`, `xs_sha1_hex()`, `xs_sha256_hex()`, `xs_sha256_base64()`
  - `xs_base64_enc()`, `xs_base64_dec()`
  - `xs_evp_genkey()`, `xs_evp_sign()`, `xs_evp_verify()`
- **Used by**: `activitypub.c`, `http.c`, `httpd.c`, `html.c`, `data.c`, `mastoapi.c`, `main.c`, `rss.c`, `snac.c`, `utils.c`
- **ESP32 plan**:
  - Replace OpenSSL backend with mbedTLS while keeping the public API.
  - Keys remain PEM strings to match existing storage format.

### Filesystem / DB assumptions

- **Key files**: `data.c`, `activitypub.c`
- **Unix primitives to replace/guard**:
  - `flock()` used for index and JSON file read/write coordination (ESP32: rely on `data_mutex`, make flock a no-op).
  - `link()` used for atomic-ish file fanout / backups (ESP32: emulate hardlink by copying files).
  - `st_ctim` access in `f_ctime()` (ESP32: use `st_mtime` where ctime is unavailable).

### Concurrency / daemon assumptions

- **Key file**: `httpd.c`
- **Unix primitives to guard**:
  - shared memory (`shm_open`, `mmap`) -> build with `WITHOUT_SHM` on ESP32
  - pidfile locking (`lockf`) -> not applicable on ESP32
  - signals / rlimit / sysconf -> optional; skip where unsupported
  - threading: prefer ESP-IDF pthreads for minimal change initially

### Media processing

- **Seam**: `strip_media()` in `snac.c`
- **ESP32 plan**:
  - Replace `system(mogrify/ffmpeg)` with a host-PC offload path (WebSocket relay).
  - MVP: allow “strip disabled” or no-op until relay exists.

