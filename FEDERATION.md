# Federation

## Protocols and standards

- [ActivityPub](https://www.w3.org/TR/activitypub/) (S2S)
- [WebFinger](https://webfinger.net/)
- [HTTP Signatures](https://datatracker.ietf.org/doc/html/draft-cavage-http-signatures).
  - Outgoing requests are RSA-SHA256 signed; incoming requests must verify, including optional `(created)` and `(expires)` components.
- [NodeInfo](https://nodeinfo.diaspora.software/)
  - 2.0 and 2.1.
- [Webmention](https://www.w3.org/TR/webmention/)
  - Receive-only. `/webmention-hook` accepts notifications and records them for relevant timelines.

## FEP support

- [FEP-f1d5: NodeInfo in Fediverse Software](https://codeberg.org/fediverse/fep/src/branch/main/fep/f1d5/fep-f1d5.md)
- [FEP-67ff: FEDERATION.md](https://codeberg.org/fediverse/fep/src/branch/main/fep/67ff/fep-67ff.md)

## ActivityPub details

### Actor discovery

- Local actor IDs match `https://<domain>/<user>` and the same URL is persisted as the canonical identifier.
- WebFinger exposes `acct:<user>@<domain>` entries using the host-meta template.
- `/authorize_interaction` and `/share` endpoints are available.

### Collections and endpoints

- `GET /<user>` returns the actor document (`application/ld+json`) including inbox/outbox/followers/following/featured collection URLs and the actor's public key.
- `GET /<user>/outbox` exposes most recent public posts in `orderedItems`.
- `GET /<user>/featured` publishes user's pinned posts collection.
- `GET /<user>/followers` and `/following` return empty collections unless the user enables `show_contact_metrics`, in which case only totals are shared.
- Individual public objects are available via `/p/<id>`, and their replies collections under `/r/<id>`, with optional paging.
- `POST /<user>/inbox` and `/shared-inbox` accept signed JSON objects; shared inbox traffic is queued for the appropriate local recipients.

### Supported activities and objects

- **Inbound**: `Follow`, `Accept`, `Create`, `Update`, `Delete`, `Announce`, `Like`, `EmojiReact`, `Undo` (for Follow/Like/Announce), and `Move` covering `Note`, `Question`, `Page`, `Article`, `Event`, and `Video`.
- **Outbound**: the same set, except for `EmojiReact` (normalised into `Like` internally) and `Create` currently emits `Note` and `Question` objects.
- Ordered collections: outboxes present the latest entries; follower/following collections hide membership by default.

### Delivery and moderation

- Outbound requests are signed over `(request-target) host digest date`; inbound signatures must validate or message is dropped.
- `Digest` headers on inbound POSTs are checked; mismatches receive HTTP 400.
- Deliveries use a retry queue with parameters `queue_retry_max`, `queue_retry_minutes`, `queue_timeout`, and `queue_timeout_2` in `server.json`.
- Messages are rejected if they originate from muted or limited actors, blocked hashtags, or blocked servers.

### Audience and account features

- Visibility modes include Public, Unlisted, Followers-only, and Direct (addressed only to mentioned accounts). Direct replies retain the mention list.
- Users may require follow approvals, hide follower counts, mark accounts as bots, and adjust other publishing preferences.
- Account migration emits ActivityPub `Move` messages to followers and follows the new identity when a compliant `Move` is received.

### Shared inbox handling

- `/shared-inbox` receives the same signature verification, queuing, and moderation treatment as user inboxes.

## Additional documentation

- Full documentation: <https://comam.es/snac-doc/>
  - User manual: `doc/snac.1`
  - Data formats: `doc/snac.5`
  - Administrator manual: `doc/snac.8`
- Release history and other notes: `RELEASE_NOTES.md`
