# USB companion protocol

The browser opens the existing CDC port at 115200 baud and sends
`RSVPNANO/COMPANION/1\n`. The reader answers with `READY`, `BUSY MSC`, or
`UNSUPPORTED` on the same line. Companion mode never changes line signals or
opens the reset-triggering 1200-baud rate.

After `READY`, traffic uses resynchronizable `RSV2` frames:

| Offset | Field | Size |
| --- | --- | --- |
| 0 | ASCII `RSV2` | 4 bytes |
| 4 | protocol version (`1`) | 1 byte |
| 5 | frame type | 1 byte |
| 6 | request ID, little endian | 4 bytes |
| 10 | sequence, little endian | 4 bytes |
| 14 | payload length, little endian | 4 bytes |
| 18 | payload | variable |
| end | CRC32 over version through payload | 4 bytes |

Frame types are request, data, end, response, acknowledgement, error, ping,
pong, and close. Requests and responses use 4 KiB data frames with one frame
in flight. JSON request metadata carries `method`, `path`, `query`,
`contentType`, and `totalBytes`; response metadata carries `status`,
`contentType`, and `totalBytes`. The end payload carries `totalBytes` and
`crc32`.

Large request bodies are spooled to a temporary SD file, checked for final
size and CRC, then passed to the same endpoint operations and temporary-upload
logic used by HTTP. Cancellation, checksum failure, disconnect, and the
15-second traffic timeout remove the spool. Settings persistence remains
deferred to `SettingsStore::update()` on the app loop.

Improv Serial is recognized before a companion handshake. It only provisions
Wi-Fi and returns the resulting LAN URL; management continues through HTTP or
the companion protocol.
