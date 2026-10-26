# Sound Lab: rethinking the visual design

> Historical: the design exploration that led to the current app. The rules in force are in [docs/DESIGN.md](../docs/DESIGN.md).

Working prototypes are in [app/design/concepts/](../app/design/concepts/index.html). Each one runs on the same real model and simulated speaker as the app. Add `#edit` to a prototype's URL to open its editing mode directly. Screenshots at 1280 × 800 are in `app/design/concepts/shots/`.

## What is wrong with the current screen

- **The composition is a dashboard.** It has three columns, everything is visible at once, and every element sits in a rounded rectangle. Remove the colour and nothing is left that belongs to a speaker.
- **The graph is the hero, but it is passive.** About 55% of the window is a chart, yet in normal use you only switch presets. A large plot with faint grid lines reads as monitoring, not as playing an instrument.
- **The factory presets look like six cards.** They have the same size, weight and treatment. There is no numbering, no sense of a program bank, and no physical logic.
- **The nine band readouts look like KPI tiles.** A number above a tiny arc gauge describes a fader without being one.
- **The EQ button gets the least interesting treatment.** It is the speaker's most distinctive feature, yet it is a sidebar of three list items. The speaker itself appears nowhere.
- **The title block reads like a database inspector.** Columns labelled PROFILE, REV and SPEAKER describe the data rather than the instrument.
- **The typography is one register.** It is all tiny uppercase monospace, so the "technical" look comes from type rather than from structure.
- **The palette is the default.** Turquoise phosphor on near-black has become the AI "audio tool" look.

The references were borrowed at the surface: keycaps from Teenage Engineering, a glow from FabFilter. What actually makes those references work is a single physical idea that organises the whole interface. Wires, for example, is literally the recorder's own panel. Synplant grows everything from one seed in the centre.

## The three concepts

### A: Control surface

[a.html](../app/design/concepts/a.html)

An ivory instrument with one dark panel.
- **Listening mode**
  - The XSound Plus 2 sits in the middle, drawn from the product: the extruded pill, the fabric front and the seven top buttons.
  - Callout lines run from its EQ button to three labels, Light off, White light and Blue light. Each label shows the program number and name that the light selects. The lamp that is playing is marked.
  - Below, a charcoal program bank holds numbered keys: 01–06 factory and 07+ yours. Each key has an LED for "playing", and a small LCD shows the current curve.
  - Pressing a key plays it.
  - To change a light, click its label (or drag a key onto it) and the bank offers its keys. One more press writes it.
- **Editing mode**
  - EDIT unfolds the LCD into a full-width display, using a view transition.
  - Nine physical faders appear, with caps colour-coded low, mid and high, an engraved scale and segment readouts.
  - Hardware keys: Save, Revert, Duplicate, Reset, Apply and Delete.
  - In this mode the bank loads programs into the editor instead of playing them.

### B: Orbit

[b.html](../app/design/concepts/b.html)

One circular object.
- **The centre** is the speaker's own round EQ button, with its light ring.
- **The EQ response** is drawn in polar form around it. Frequency sweeps 270° like a knob, from 80 Hz at lower left to 13 kHz at lower right, and the distance from the centre is gain.
- **The three lights** are coloured arcs on the outer ring, each labelled with its program.
- **Programs** are numbered tokens, factory on the left and yours on the right.
  - Tap a token to play it.
  - Drag a token onto an arc to assign it, or into the centre to edit it. Clicking an arc and then a token does the same without dragging.
- **Editing** puts handles on the nine spokes, which you drag outward and inward.

### C: Bench unit

[c.html](../app/design/concepts/c.html)

The front panel of a 1970s measuring instrument, in warm enamel with engraved condensed type. It is built from stacked sections rather than cards.
- **Nameplate:** an analogue battery needle meter and a link bulb.
- **Main section:**
  - A chart-recorder window where an ink pen draws the response on graph paper.
  - A rotary program selector with six factory detents and MEM.
  - A red 7-segment memory register for your profiles.
- **EQ button section:**
  - A service-manual line drawing of the speaker.
  - Three lamp push-buttons, each with a paper label strip.
  - Assigning works the way old gear stored settings: select a program, press STORE, then press a lamp.
- **Editing:** a CALIBRATE toggle slides out a drawer of nine slide pots with segment readouts, plus Store to mem and Transmit.

## Evaluation

Ratings are relative, from 1 to 5.

| | A · Control surface | B · Orbit | C · Bench unit |
|---|---|---|---|
| Originality | 4 | 5 | 4 |
| Functionality | 5: every flow is one or two presses | 3: polar editing is less precise; long names don't fit on arcs | 4: complete, but STORE then light is an extra step |
| Desktop usability | 5: reads left to right; keyboard and screen-reader friendly | 3: depends on drag; text curved on arcs is harder to read | 4: dense but clear; small engraved labels |
| Relationship to the hardware | 5: the actual speaker is the map of its button | 4: the button is the centre, but the speaker appears only as a thumbnail | 4: the drawing and lamp buttons are faithful; the rest is generic lab gear |
| Visual coherence | 4 | 5 | 5 |
| Implementation complexity | Medium: grid, SVG speaker, faders, view transitions | High: custom polar geometry, curved text, hit-testing | Medium–high: many bespoke widgets (meter, rotary, 7-segment) |

## Recommendation: A, with two things borrowed from C

**A** is the only concept where the speaker itself organises the screen. What you see is the button you press on the real speaker and what each light will do, so it stays readable as a screenshot even without colour. It has a clear split between listening and editing, the editor is a real control surface rather than a chart, and it's the cheapest to build well.

From **C**:
- The pen-drawn trace when a curve changes; the curve should "arrive" rather than just morph.
- The paper label-strip treatment for the light assignments, which suits what they are: labels you've written for a button.

From **B**:
- The idea that the round EQ button is a recurring motif. A small drawing of it can mark the lights throughout the app.

## Effect on the code

- **Kept unchanged:** the state model (`app/js/model.js`, with its tests), the bridge (`app/js/bridge.js`), the simulated speaker (`app/js/mock-backend.js`), the native controller and the widget.
- **The concepts already run on them;** `core.js` is a thin store over the model.
- **Replaced:** only the presentation, meaning `app/js/app.js` and `app/styles/app.css` (plain JavaScript and CSS; there is no React).
- **Next step:** turn concept A into those two files, then do the native WebView2 wiring.
