# Tribit Connect

Windows controls for the Tribit XSound Plus 2 speaker, modelled on
[Sound Connect](https://github.com/KanuTomer/Sound-Connect). Independent project;
not affiliated with Tribit or Microsoft. MIT licensed.

Status: early development build. A Win + W widget (medium and large) and a
companion app, plus a protocol probe.

- Battery, connection state and the six EQ presets (XBass Off, XBass, MaxSound
  traditional, Rock, Audiobook, Jazz), with each preset's EQ curve.
- Voice prompt beeps, auto shutdown and the EQ button's light-to-preset mapping
  (companion app). Every change counts only once the speaker reports it back.
- Not yet: the custom EQ editor, the shutdown countdown, and shutdown, factory reset
  and firmware upgrade, which stay in the Tribit app.

The speaker accepts one control connection at a time. While the widget or companion
app holds it, the Tribit app on your phone cannot connect; turn on release in
Settings or close the controls first.

## Install a release

Microsoft.WindowsAppRuntime.2 2.5.1 or newer and the Windows Web Experience Pack
(Widgets) are required.
The package is self-signed, so trust its certificate once:

1. Download `TribitConnect_<version>.msix` and `TribitConnect.Local.cer` from
   [Releases](https://github.com/KanuTomer/tribit-widget/releases).
2. Open an administrator PowerShell in the download folder and run
   `Import-Certificate -FilePath TribitConnect.Local.cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople`.
3. Open the `.msix` file and select **Install**.
4. Add **XSound Plus 2** from Win + W > Add widgets.

## Install for development

Developer Mode, Windows Web Experience Pack and Microsoft.WindowsAppRuntime.2
2.5.1 or newer are required.

```powershell
./scripts/build.ps1 -Check
./scripts/package.ps1 -Register
```

Then add **XSound Plus 2** from Win + W > Add widgets, or open **Tribit Connect**
from Start. Without `-Register`, `package.ps1` builds a self-signed MSIX instead.

## Widget mock

For layout work, `./scripts/mock.ps1` exports every widget card (all sizes, pages and
connection states) from the real card code in about five seconds and writes
`build/mock/index.html`, which renders them with the Adaptive Cards renderer at the
board's widget sizes, in dark or light theme. Navigation and preset buttons work, and
each widget reports how much space is free or clipped. Refresh the page after each run.
The mock approximates the Windows host (fonts, hover and rounding), so confirm the
final result on the board with `package.ps1 -Register`.

## Protocol

The speaker is controlled over Bluetooth Classic RFCOMM channel 3 with a small
checksummed frame format, decoded from an Android HCI snoop capture of the Tribit
app. See [PROTOCOL.md](PROTOCOL.md). The scrubbed capture is in
[captures/](captures/), so you can check the decoding or continue it. Open questions: the countdown-to-shutdown
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
