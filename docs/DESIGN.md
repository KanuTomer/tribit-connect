# Tribit Connect: design system and philosophy

Audience: coding agents (Claude Code, Codex) changing the Sound Lab companion app (`app/`) or the
widget (`src/widget_*.hpp`). Dense by intent. Read it before any visual or interaction change; most
rules below exist because the owner rejected the alternative. When the code and this file disagree,
the code is the truth for behaviour and this file for intent: fix one or the other in the same change.

## 1. Thesis

The app is **one physical instrument**, not a dashboard. It depicts the real XSound Plus 2 and the
real controls it has (three EQ-button lights, presets, a custom curve) as manufactured hardware:
an ivory or graphite chassis, one charcoal panel (the program bank), recessed LCDs, keys that travel,
lamps that glow. Every visual element must either **carry state** or **be a control**. Decoration
that does neither is removed.

Owner taste, in order of weight:

1. **No redundancy.** Never say a thing twice. If a lit key, lamp or wire already shows state,
   no text repeats it. This rule has removed: "Stored", "Playing now", "Step 1–3", the LCD program
   name, the editor profile number, "Saved r2", "Speaker plays the saved version · on the blue light",
   the AUDITION annunciator (the Audition key has its own lamp), the "Device" heading inside the
   Device sheet, the subtitle "Tribit Connect", battery % in Settings (it is in the title bar),
   persistent instruction text under the speaker, "Saved on speaker". Before adding any label, find
   what else on screen already communicates it. Statuses appear only when they are exceptions
   (choose a program, writing, profile changed, set elsewhere, not read yet, last known, PAUSED, OFFLINE).
2. **Not "AI-generated".** Rejected outright: black and turquoise, generic rounded cards, gradient
   blobs, monospace everywhere, emoji, gratuitous animation, glassmorphism, settings-as-dropdown-soup.
   References that are welcome: Teenage Engineering (OP-1, EP-133), FabFilter Pro-Q, selective vintage
   instrumentation (needle meter, 7-segment, engraved legends), expensive audio hardware rather than a
   skeuomorphic simulation of it.
3. **Restraint over texture.** Grain textures were removed (`--tex:none`). Calibration marks stay
   because they are functional (dB scale, 0/±4/±8 rules); decorative rules go (17 fader rules became 5;
   the duplicate right-hand dB scale was removed). Never stack texture + shadow + border + inset on one
   surface; one edge and one highlight is the budget.
4. **Smooth everywhere.** Every state change animates (see §6). The owner notices snaps, flashes,
   jumps and wires that stretch mid-transition. Rebuilding DOM kills CSS transitions; update in place.
5. **Consistency of controls.** One key family (see §4). Different thickness, corner radius or drop
   between buttons in one view was called out as a defect.
6. **Honesty.** Never imply state the app does not know. Offline shows "Last known", the meter parks
   at `--`, the visualizer only claims PAUSED when it is actually listening to the speaker's output,
   factory curves are marked ≈ (they are traced drawings, not band data).

## 2. Information architecture

```
Title bar (58px, = window caption, app-region:drag)
  model name · [mini speaker, edit mode only] · battery needle meter · link bulb + word · SETTINGS · min/max/close
Stage (listening)                       | Surface (editing)
  three assignment modules (one per     |   name input · single state lamp (SAVED|DRAFT|FACTORY)
  EQ-button light) wired by conduits    |   fader plate: LCD display in band columns, 9 channels,
  to the drawn speaker's EQ button      |   one dB scale (left), LOW/MID/HIGH brackets, transport
  offline card over speaker when needed |
Program bank (168px, charcoal, always visible)
  PROGRAM legend · FACTORY 01–06 · YOURS 07+ (paged, reorderable) + "+ new" · LCD (curve + spectrum) · EDIT/DONE
Settings sheet (overlay, right, 368px): Speaker · Windows · Appearance · Reconnect/Bluetooth · note
```

Key concepts and their separation (never blur these):

- **Play / Apply**: the speaker plays a program now (preset id, or custom bands via 0x82).
- **Save**: stores a profile on this PC (`LocalState/profiles.json`). Never touches the speaker.
- **Assign**: writes what an EQ-button light selects (0x18, all three lights in one write).
- **Audition**: while its lamp is lit, every fader move streams the curve to the speaker
  (latest-wins queue). The speaker plays an unsaved draft **only** while Audition is lit. Leaving the
  profile/editor undoes what Audition played.
- **Draft**: unsaved edits, auto-saved with the profile (`profile.draft`), survive restarts, cleared by
  Save or Revert (Revert offers Undo). No confirm dialogs, ever (`confirm()` was rejected as
  "app.tribit says…").
- **Widget buttons**: six slots on the Windows widget, each a factory preset or a saved profile
  (saved version). Chosen in Settings → Windows from a 3×2 replica of the widget, not dropdowns.

## 3. Tokens (`app/styles/app.css` `:root` and `:root[data-theme=dark]`)

Colour has jobs. Do not introduce a colour without a job.

| Role | Token(s) | Job |
|---|---|---|
| Chassis | `--ivory` (light #ece8de / dark #22221f), `--well`, `--alu` (plate), `--paper` (sheet, cards) | surfaces |
| Ink | `--ink`, `--ink-2`, `--ink-3`, `--engrave` | text tiers; `--engrave` for engraved legends |
| Panel | `--panel` | the program bank only (the one dark panel; in dark theme it is darker still) |
| Keys | `--key`, `--key-user`, `--key-edge`, `--key-ink` | program keys stay light in both themes; `--key-ink` is fixed dark |
| Act / attention | `--orange` #ff5a14, `--orange-edge` | primary action (EDIT, Play), focus ring, armed, active lamps in Settings |
| Playing / draft | `--amber` #ffb21a | playing LED on keys, draft dot, writing |
| Bands | `--low` #2f6fe4, `--mid` #2f9e5b, `--high` #e4572e | fader caps and LOW/MID/HIGH brackets only |
| Lights | `Speaker.LIGHT` ['#4a4b51','#f7f8fb','#3d8bff'], `WIRE` ['#9b9ca4','#f6f6f1','#4d95ff'] | the speaker's three EQ-button lights; live conduit core colour |
| LCD | `--lcd` #22271e, `--lcd-ink` #d5ecab, `--lcd-dim`, `--lcd-ghost` | every display, both themes |
| Relief | `--lip` (lit lower edge of recesses), `--tlip` (legend text shadow), `--plate-hi`, `--plate-edge`, `--groove` | the only depth vocabulary; dark theme inverts `--tlip` to a dark lip |
| Mechanical keys | `--mech-a/-b` (gradient), `--mech-edge`, `--mech-hi` | every `.mech`, `.chip`, `.pk`, `.wtile`, `.dv-seg` |
| Off states | `--led-off`, `--win-off`, `--sw-off`, `--bezel` | unlit lamps, switch track, lamp-key bezel |
| Motion | `--ease` cubic-bezier(.2,.75,.2,1) | all eased motion |
| Geometry | `--fh` clamp(96px, (100vh−452px)·.55, 240px) | fader travel follows window height |

Theme: `js/theme.js` runs in `<head>` before CSS paints; reads `localStorage['tribit-connect.theme']`
(`light|dark|device`), sets `html[data-theme]`, posts `{type:'theme',dark}` to the host (DWM dark frame,
WebView background; host persists `LocalState/theme` to avoid a startup flash). Changes cross-fade via
`:root.theming` for 450 ms. Never hard-code a colour that differs between themes; add a token to both blocks.

Type: Archivo (variable width axis; `font-stretch` 80–125% does the condensed/extended work),
Doto (dot-matrix, LCD text only), DSEG7 Classic Bold as `Seg` (numeric readouts only). Engraved legends
use `.engr` (600, stretch 120%, 10.5px, .16em tracking, uppercase, `--engrave`, `0 1px 0 var(--tlip)`).
No other fonts. Fonts are OFL and shipped in `app/fonts/` with licences.

## 4. Components (inventory and rules)

- **Key family** (`.mech`, `.chip`, `.pk`, `.wtile`, `.dv-seg`, `.pager`-adjacent): 1px `--mech-edge`
  border, 5px radius, `0 2px 0 var(--mech-edge)` drop, `inset 0 1px 0 var(--mech-hi)`, gradient
  `--mech-a→--mech-b`. `:active` translates 2px and removes the drop. Lamps are 6px dots via `::before`
  (`--win-off` off, `--orange` + glow on). Do not create a button outside this family.
- **Program key** (`.pkey`): 84px tall (72px under 700px window height), width clamp(62px,5.6vw,80px),
  number (Archivo 700 24px) + name; amber LED strip above (`.led`), light dots top-right
  (`.dot i.l0/l1/l2`), amber draft dot after the number (`.dm`), `.editing` = inset orange rim,
  `.playing` = amber LED. Factory keys `--key`, user keys `--key-user`.
- **YOURS row**: whole keys only. When it overflows, `.userwrap.paged` shows solid page keys
  (`.pager`, 30px) either side and `fit()` sizes the row to an integer number of keys. A page key at its
  end folds to width 0 (no padding!) and the YOURS label shifts with it (`.group.user.shifted`).
  Scroll-snap per key. **No fades over keys, no partially covered keys, no wheel scrolling** (all rejected).
  Reorder by HTML5 drag (FLIP `slide()`, `.lifted` dashed placeholder, `.sorting` disables snap) or
  Alt+←/→; numbers follow order.
- **Assignment module** (`.unit`): engraved light name, recessed lamp key (`.lampkey .face`, the light's
  colour; lit = glow including the "off" light, which gets a neutral glow, never a box), 7-seg number +
  Doto name readout (content vertically centred), foot status only for exceptions.
- **Conduits**: engraved groove (`.lip` + `.cut`) from each module's bottom centre, cubic into the EQ
  ring, arriving radially and ending exactly on the ring's outer edge (`out = rr·1.09`; masks were tried and
  rendered unreliably). Three live overlays (`.wire[data-w]`) exist permanently; `.on` fades the lit one
  (groove 3.2px + 6px glow at .2 + 1.6px core). Geometry is measured in layout space (`offsetTop/Left` and
  the ring's offset within `.spkwrap`) so mode-change transforms never stretch wires; re-measured on
  `animationend` of `.spkwrap` and on resize.
- **Speaker drawing** (`js/speaker.js`): one silhouette path (back stadium + sides + front stadium,
  never a side rect), body gradient, rim highlights fading in 8% of height, mesh pattern grille with **no**
  shade gradient, TRIBIT badge, seven glyphs. EQ button r 9.5, ring 1.7px coloured by `ring(light)`, halo is a
  radial gradient per light. Built once; `Speaker.set(root,{light,state})` changes halo opacity, ring stroke
  and BT LED in place so they fade. The infinity glyph is a symmetric figure eight; keep glyphs symmetric.
- **Fader plate**: `.fplate` grid `40px repeat(9) 10px transport`; LCD display spans the 9 channel columns
  so each band's point sits above its fader (`curveByBand`, `plotter` with `inset:5`). Whole channel column
  is the pointer target (`[data-chan]`, `preventDefault` on pointerdown, `user-select:none`); double-click
  zeroes only on the cap. Value readout `.val` 7-seg, flex-centred. Hover highlights a column on plate and
  display together (`setHot`).
- **Displays**: grid stroke `--lcd-ink` at 7% (zero line 22%), curve #e6f6c6 2.4px, no glow, fill ~4%.
  Bank LCD: curve drawn by a pen (nib, 700 ms) when the program changes; faint spectrum bars (`.bars`,
  12% `--lcd-ink`) in band columns behind it; top-right state text only `PAUSED`/`OFFLINE`.
- **Settings sheet**: `section`s Speaker (beep chips, auto shutdown switch), Windows (widget 3×2 grid →
  slide-open `.dv-pick` list, two columns, max-height 190px scroll, "Yours · N" count; phone release;
  diagnostic log), Appearance (Light/Dark/Windows segmented). Switch rows: bold label + one-line hint in
  plain language (never implementation detail such as "15 s after the widget hides"). Sheet is updated with
  `morph()` (attribute/text diff), never `innerHTML`, so switches slide and lamps fade.
- **Title bar**: the mini speaker appears left of the meter in edit mode so the right-hand cluster
  (meter, link, SETTINGS, window buttons) never shifts. Link word is `Linked` throughout writes
  (writes show on the key/module being written; the bulb flashing "Writing" was rejected).
- **Offline**: `.app.offline` greys speaker/lamps/meter/keys, conduits fade, offline card (engraved title,
  plain cause, Reconnect / Bluetooth) rises over the speaker. Editing still works (profiles are local).

## 5. State model (frontend)

- `js/model.js`: pure, unit-tested (`node --test app/tests/model.test.js`). Library
  `{profiles:[{key,name,bands,rev,draft?,basedOn,approx}], links:{custom,slots[3]}, widget:[6 keys]}`.
  `activeKey` identifies the speaker's custom curve by band content. `slotView` → synced/stale/changed/unknown.
  `moveProfile`, `setDraft`, `widgetSlots` (resolved for the host: factory `{preset}`, saved
  `{preset:254,name,bands}`).
- `js/store.js`: the only writer. `persist()` sends `saveLibrary` with `widgetSlots` attached; drafts persist
  debounced 500 ms. Audition queue: single-flight, latest wins, restores through the same queue; `apply`
  waits for it. `Store.transition(fn)`: `.leaving` 150 ms → `fn()` → `.arriving` 700 ms (timers, not frames,
  so a hidden window cannot stall it; instant under reduced motion).
- `js/app.js`: views. Rule: **build once, update in place.** `instruments()`, `speakerArt()`, `units()`,
  conduits, bank keys (`dataset.html` signature; playing/busy/editing toggled), sheet (`morph`). Only rebuild
  when structure truly changes.
- `js/bridge.js`: WebView2 `postMessage` or `MockBackend` (browser). Same JSON both ways; `request()`
  resolves by id with the host's result *after* the state it produced.

## 6. Motion rules

- Durations: press 70–80 ms; lamp/LED/halo/wire 250–500 ms; mode change 150 ms out + ≤700 ms in;
  sheet open 300 ms / close 160 ms; picker grid-rows 300 ms; theme 400 ms; pen 700 ms; fader cap 450 ms.
- Arrivals rise 10–12 px with `--ease`, staggered in reading order (modules 0/50/100 ms, channels
  `--i`·22 ms + 60 ms). Leavings are short fades; nothing slides out far.
- Never animate by rebuilding. Never let geometry derived from a transformed element persist
  (measure layout, not boxes, during transitions).
- `prefers-reduced-motion`: everything instant (global rule at the end of app.css); scroll behaviour `auto`.
- Size changes animate (`grid-template-rows: 0fr→1fr`, width transitions on folding page keys).

## 7. Copy rules

- Sentence case for UI text, uppercase only for engraved legends and LCD text.
- Name the thing, not the mechanism ("Let the phone app connect", not "Release RFCOMM").
- One short hint per switch at most. No persistent instructions; no "click here to…".
- Errors say what to do ("Turn it on and check it is paired with this PC. It takes one controller at a
  time, so close the Tribit app on your phone.").
- Unknown protocol features are acknowledged once, at the bottom of Settings.

## 8. Widget (Windows Widgets board, Adaptive Cards)

Separate constraints, see `src/widget_card.hpp`: template and data are split; the board rebuilds the
card on every update and blanks images while reloading, so: one update per click, identical data on
refresh sends nothing, no images inside buttons, selected button = bold Accent text on the host's neutral
surface (colour washes rejected). Six buttons are data-driven (`eq.slot.N`, data `sN.label/...`); saved
curves render orange via `custom_artwork()`. The curve image blinking on change is an accepted board limit.

## 9. Verification workflow (mandatory for visual changes)

1. Iterate in the browser mock: open `app/index.html` (file:// works). Hash/query helpers: `#edit`,
   `#device`, `?offline`, `?music` (fake spectrum), `?many` (full YOURS row), `?debug` or backtick for the
   mock strip.
2. Screenshot with headless Edge and **inspect zoomed crops of the exact element changed**, in both themes:
   `msedge --headless=new --user-data-dir=<tmp> --window-size=1280,800 --hide-scrollbars
   --virtual-time-budget=4000 [--force-dark-mode] [--force-prefers-reduced-motion] --screenshot=<png> <url>`
   via `Start-Process … -PassThru; WaitForExit(40000)` (Edge sometimes does not exit). Crop with System.Drawing.
3. Behaviour probes: inject a temporary `<script>` into a copy of `index.html` that drives the UI (clicks,
   synthetic `DragEvent` with `new DataTransfer()`, `KeyboardEvent`) and writes results to
   `document.body.dataset.probe`; run with `--dump-dom` and grep. Query the live document, not
   detached nodes. Delete the temporary files.
4. Known artefacts: virtual time may capture CSS transitions mid-way (fader caps, smooth scroll); measure
   final geometry with a probe before calling it a bug. rAF-driven code (pen, smooth scroll, scroll events)
   may not run under `--dump-dom`.
5. Sizes to check: 960×620 (minimum), 1280×800, 1366×768, 1920×1080.
6. Do **not** rebuild/re-register the MSIX for each small change; batch, and register when the owner asks
   (`scripts/package.ps1 -Register`, bump `packaging/AppxManifest.xml` Version every time).

## 10. Invariants (do not break)

- Speaker writes are confirmed by reading back; the speaker does not clamp, the app enforces ±8 dB.
- The speaker takes one control connection; the phone app is locked out while the app holds it.
- Assigning a light preserves the other two lights' raw bytes.
- The speaker plays an unsaved draft only while Audition is lit.
- Factory programs are read-only; their curves are approximations (≈).
- The visualizer analyses only this PC's audio to the default output when that output is the speaker;
  it never claims to know about phone playback.
- No confirm dialogs, no redundant text, no texture, no fades over controls.
