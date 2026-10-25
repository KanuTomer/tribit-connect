# Bluetooth capture

The Android HCI snoop log that [PROTOCOL.md](../PROTOCOL.md) was decoded from, recorded on 25 October 2026 while the Tribit app read the speaker and changed each setting.

- `tribit-20261025005628.btsnoop` opens in Wireshark.
- `tribit-20261025005628.txt` lists each Tribit frame with its time and direction.

The capture was reduced with [tools/scrub_btsnoop.py](../tools/scrub_btsnoop.py). Only the speaker's RFCOMM control channel (DLCI 6, server channel 3) and the RFCOMM multiplexer channel are kept. Everything else was removed: HCI commands and events (device addresses, names, link keys), SDP, audio, the hands-free channel and other devices.

To record your own capture:
1. On Android, turn on **Enable Bluetooth HCI snoop log** in Developer options.
2. Turn Bluetooth off and on.
3. Use the Tribit app.
4. Run `adb bugreport`; the log is in the zip under `FS/data/misc/bluetooth/logs/`.
5. Scrub it before you share it:

```
python tools/scrub_btsnoop.py btsnoop_hci.log scrubbed.btsnoop frames.txt
```
