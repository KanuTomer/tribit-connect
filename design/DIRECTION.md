# Sound Lab: design notes

> Historical: the design exploration that led to the current app. The rules in force are in [docs/DESIGN.md](../docs/DESIGN.md).

These are the reference findings and the three candidate directions. The live board is [app/design/directions.html](../app/design/directions.html); add `#B` or `#C` to the URL to open another direction.

## What the references actually do

- **FabFilter Pro-Q 4:**
  - The EQ display fills the window, and every other control sits on top of it or right next to it.
  - Band nodes are small rings that fill in when grabbed. A value readout appears beside the node only while you hover or drag.
  - Grid labels are tiny and sit on the edges, so the curve dominates.
  - "EQ Sketch" sets bands by drawing the whole curve in one stroke. That suits 9 fixed bands well.
  - Smooth parameter interpolation means the curve glides between states instead of jumping.
- **Teenage Engineering OP-1 Field and EP-133:**
  - A warm grey body with off-white keycaps that press down physically.
  - Four colour-coded controls (blue, green, white, orange) whose colours carry meaning throughout.
  - Tiny lowercase labels, and one small high-contrast screen with playful vector drawings.
  - Wide outline display type with a katakana accent.
  - The personality comes from restraint: few colours, honest controls.
- **MiniMeters:**
  - Dense metering in a thin strip, using monospace type and frequency-coloured waveforms.
  - It captures real system output, so a live spectrum from Windows' loopback audio is feasible and honest. That would be a later, optional stage.
- **Winamp skins:**
  - The classic EQ window is a curve preview strip above the band sliders, with bitmap LCD readouts.
  - The proof that a desktop app can have a body, not just panels.
- **Poolsuite, Hoverstat.es, Recent, Cosmos:**
  - These are galleries and moods rather than patterns. The lesson I took: commit to one material metaphor and let the typography carry it.
  - Mobbin requires a sign-in, so I didn't use it.

## Directions

**A. Bench instrument** (the FabFilter and Teenage Engineering hybrid)
- A graphite chassis with a phosphor-turquoise EQ "glass" in the centre.
- Off-white keycaps for the factory presets, and a yellow LED on the selected one.
- Lamp buttons for the three EQ button slots, plus band readouts with arc meters.
- Type: Martian Mono.
- Strength: the most precise and legible, and it fits a desktop window naturally.
- Risk: the closest of the three to "dark audio app"; it needs the hardware details to stay distinctive.

**B. Field display**
- The whole app as one instrument: a black LCD with dot-matrix type (Doto).
- Soft keys around the screen, segmented band meters, and colour-coded encoders.
- Strength: the most character and the most fun.
- Risks:
  - the fixed screen metaphor fights resizing;
  - dense settings get cramped;
  - dot-matrix type is weaker for accessibility.

**C. Measurement sheet**
- A light technical drawing: ink curve on graph paper, red-pencil annotations for edits, and blue stamps for the hardware slots.
- A title block holds the instrument details and revisions.
- Type: IBM Plex Mono and Instrument Serif italic.
- Strength: original, calm, and it makes revision and sync status feel native ("rev 4, resync").
- Risk: less "instrument", more "document".

**Recommendation:** A as the base. Bring in B's colour-coded hardware language for the three light slots and the band encoders, and C's revision and title-block idea for sync state.

**Chosen (2026-10-26):** A as the base, with B's colour-coded hardware (low, mid and high bands in blue, green and orange; lamps for the three lights) and C's title block for revision and sync. The app is in [app/](../app/index.html).

## Hardware facts the design rests on (probed 2026-10-26)

- Bands: 80, 150, 300, 600, 1.2k, 2.5k, 5k, 9k and 13k Hz, ±8 dB in 1 dB steps.
- The speaker holds one applied custom curve, plus one curve per EQ-button light. Every light can carry a custom curve.
- Both the applied curve and the light curves survive a power cycle.
