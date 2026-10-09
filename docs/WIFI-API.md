# Local HTTP API v1

The reader serves HTTP on `192.168.4.1:80` and a bounded wildcard DNS responder on UDP 53. Unknown HTTP hosts redirect to the local page. Other origins are rejected; there is no CORS permission. UI assets are compiled into firmware and need no SD files or CDN.

`GET /api/session` returns product `InkReader`, API `1`, a random session token and whether the clock is enabled. API requests require Host `192.168.4.1` (optionally `:80`). Never log the token or credential bodies.

`POST /api/op` requires `X-Reader-Token`, decimal `X-Reader-Operation` and nonzero 32-bit `X-Reader-Request`. Maximum body is 4,104 bytes. JSON is UTF-8; binary integers are unsigned little-endian. A reused last request ID/body/opcode returns its cached response; conflicting reuse is rejected. The latest upload chunk can also be retried by offset/CRC after intervening requests. The bundled browser retries transport failures at most three times with the same ID. Logical errors are not retried.

| Operation | Body / response |
|---|---|
| 1 Identify | `{}` → product, firmware and 4096-byte chunk limit |
| 2 List | `{path,cursor:0}` → entries and `next` cursor, `-1` at end |
| 3 Space | `{}` → total/free bytes |
| 4 Begin upload | `{path,size,crc,overwrite}`; max 512 MiB, one operation globally |
| 5 Upload chunk | offset (4 bytes), chunk CRC32 (4 bytes), up to 4096 data bytes → acknowledged offset |
| 6 Commit | `{}`; verifies length/running CRC, rereads temporary file, journals and verifies replacement |
| 7 Begin download | `{path}` → size/CRC32 |
| 8 Download chunk | offset/count (4 bytes each) → offset (4 bytes) plus data |
| 9 Folder | `{path}` creates a folder |
| 10 Delete | `{path}` deletes a file or empty folder |
| 11 Cancel | `{}` closes handles and safely recovers temporary state |
| 12 Finish | `{}` cancels and exits transfer mode |
| 13 Probe | `{}`; does not reset idle deadline |
| 14 Clock config | `{ssid,password}` or `{clear:true}`; absent clock builds reject it |
| 15 User activity | `{}` from genuine pointer/keyboard activity; no automatic heartbeat |

Responses are JSON except successful operation 8. Failures return JSON `{error}` with HTTP 400; existing destination conflicts return 409. CRC32 uses IEEE polynomial `0xEDB88320`, initial/final XOR `0xFFFFFFFF`.

All SD work executes on the reader main task. The HTTP worker queues one bounded request and waits; it never accesses reader SD state directly. Shutdown rejects queued requests and stops the worker before disabling Wi-Fi. Reserved `.crupload`/`.crbackup` files and the existing private journal retain interrupted replacement recovery. Existing destinations remain valid until commit; failed commit restores the previous copy.

## Offline PDF resources

`GET /pdf-import.js` serves the original browser import code. Exact whitelisted `/pdfjs/6.4.299/...` resources serve gzip-compressed modules, codec fallbacks, fonts, and Japanese CMaps from flash in 4 KiB chunks. Missing resource paths return 404. These routes expose no SD data. Module-worker/font CSP allowances support local rendering. PDF conversion uses operation 15 only while preparing a user-requested import; output then uses the existing upload operations 4–6. No protocol version change is required.
