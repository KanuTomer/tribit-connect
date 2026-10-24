# Tribit XSound Plus 2 control protocol (from hci_snoop20261025005628.cfa)

Transport: Bluetooth Classic RFCOMM, server channel 3 (DLCI 6). Not BLE.

Frame: `HDR 01 CMD LEN_LO LEN_HI DATA... SUM`
- HDR phone->speaker: `08 EE 00 00 00`; speaker->phone: `09 FF 00 00 01`
- LEN = total frame length including HDR and SUM (10 with no data)
- SUM = low 8 bits of the sum of every preceding byte (verified)
- Speaker acks a set by echoing CMD with no data.

| Cmd | Dir | Data | Meaning |
|---|---|---|---|
| 0x02 | get | 1 byte, 0x64 | battery % (100 while charging) - verify |
| 0x05 | get | `FE` + 9 bands | custom EQ; band value 8 = 0 dB, 0x0B = +3 dB |
| 0x06 | get | `18 0B 16 00 03 01` | device info; `00 03 01` = firmware v0.3.1 |
| 0x08 | get | `00` | probably countdown to shutdown (off) - verify |
| 0x09 | get | `08` | unknown (volume?) - verify |
| 0x11 | get | `FF FF 01` | probably auto-shutdown state - verify |
| 0x82 | set | preset id + 9 zero bytes, or `FE` + 9 bands | apply preset / custom EQ |
| 0x8C | set | `00` off / `01` on | auto shutdown |
| 0x90 | set | 5 bytes: power, BT pairing, low battery, max volume, TWS beeps (1 = on) | voice prompts |
| 0x91 | get | same 5 bytes | voice prompts |
| 0x18 | set | per light: `03 <light 0-2> <preset id>` + 9 zero bytes | EQ button mapping (lights: off, white, blue) |
| 0x19 | get | same layout, one reply per light | EQ button mapping |

Preset ids: 0 XBass Off, 1 XBass, 3 MaxSound traditional, 5 Jazz.
Audiobook = 2 and Rock = 4 (confirmed below; the phone capture's button read had suggested the reverse).

Not captured: countdown to shutdown set command, custom EQ band frequencies
(9 bands, 80 Hz .. 13 kHz per reviews), band range limits.

## Confirmed from Windows (tribit-probe, 2026-10-25)

- Windows connects to RFCOMM channel 3 directly by port number; no SDP lookup is needed.
- The speaker silently drops a frame sent immediately after the channel opens.
  Waiting 500 ms after connecting fixes it.
- 0x05 returns the *current* EQ, not only the custom one: with XBass active it
  replied `01` + nine zero bytes. Bands carry values only when the id is `FE`.
- 0x19 replies with three frames, one per light; the Blue = MaxSound traditional
  mapping set in the app read back as `03 02 03`.
- 0x09 read `12` here versus `08` in the phone capture, so it changes at runtime
  (probably volume).
- 0x02 read `64` (100) again; whether the speaker was charging is unknown.
- Writes work from Windows: 0x90 with `01 01 01 01 01` was acked (empty 0x90 echo),
  0x91 read it back, and restoring `01 01 01 01 00` was acked and read back too.
- Preset ids confirmed by applying them from Windows and checking the app:
  2 = Audiobook, 4 = Rock. The full list is 0 XBass Off, 1 XBass, 2 Audiobook,
  3 MaxSound traditional, 4 Rock, 5 Jazz.
