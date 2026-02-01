## Host PC requirements (reverse proxy + media relay)

### Reverse proxy (TLS termination)

Federation requires HTTPS. Terminate TLS on a host PC and forward plain HTTP to the ESP32.

Example Caddyfile (edit domain/IP and ESP32 address):

- **Caddyfile**:

```
example.com {
  encode gzip
  reverse_proxy http://192.168.1.50:8001
}
```

### Media relay (future)

Snac2 normally shells out to `mogrify`/`ffmpeg` for media stripping. On ESP32 this is disabled (no-op) and should be offloaded to a host-side relay.

Proposed minimal protocol (WebSocket):
- **Request**: `{ "op": "strip", "contentType": "...", "pathHint": "...", "dataB64": "..." }`
- **Response**: `{ "ok": true, "contentType": "...", "dataB64": "..." }`

