# Tribit Connect

Windows controls for the Tribit XSound Plus 2 speaker, modelled on
[Sound Connect](https://github.com/KanuTomer/Sound-Connect). Independent project;
not affiliated with Tribit or Microsoft. MIT licensed.

Status: protocol probe only. The Win + W widget and companion app come next.

## Protocol

The speaker is controlled over Bluetooth Classic RFCOMM channel 3 with a small
checksummed frame format, decoded from an Android HCI snoop capture of the Tribit
app. See [PROTOCOL.md](PROTOCOL.md). Open questions: the countdown-to-shutdown
write, and the custom EQ band frequencies and limits.

## Probe

Pair the speaker in Windows Bluetooth settings and close the Tribit app on your
phone, because the speaker accepts one control connection at a time.

```powershell
./scripts/build.ps1 -Check
./build/bin/Release/tribit-probe.exe
```

By default the probe is read-only: it reads battery, firmware, current EQ, voice
prompts, EQ button mapping and the not-yet-identified status values, and prints
JSON. It never scans, pairs or retries.

- `--trace` prints every frame sent and received.
- `--listen SECONDS` records frames the speaker sends on its own, for example when its buttons are pressed.
- `--verify-voice-prompts` flips TWS Pairing Beep once, confirms it, then restores the original setting.
- `--preset 0-5` applies an EQ preset. It is not restored afterwards.

## Build requirements

Windows x64, Visual Studio 2022 C++ Build Tools with the CMake component, and the
Windows SDK.
