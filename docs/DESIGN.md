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
  ‹ SPEAKERS (to the Speakers page; on a speaker's page only, never on the Speakers page itself) · model name · battery needle meter · link bulb + word · SETTINGS · min/max/close
Stage (listening)                       | Surface (editing)
  three assignment modules (one per     |   name input · lamps: FACTORY (factory only), PLAYING ON THE SPEAKER
  EQ-button light) wired by conduits    |   fader plate: LCD display in band columns, 9 channels,
  to the drawn speaker's EQ button      |   one dB scale (left), LOW/MID/HIGH brackets, transport
  offline card over speaker when needed |
Program bar (168px, charcoal; covered only by All programs)
  PROGRAM legend · the first twelve programs, 01–12, no groups · "+ new" (editor only) · "All N" · LCD (curve + spectrum; folds away in the editor) · EDIT/DONE
All programs (a page: slides up over the stage AND the bar): PROGRAMS · count · search · DONE; one grid of records, six to a row, in the user's order
Settings sheet (overlay, right, 368px; SETTINGS). One key; what it holds follows the page:
  on a speaker's page: the speaker's name · firmware (and why there is no link, only then) · Try again (busy
  only) · Beeps · Auto shutdown · Let the phone app connect · note · App settings ›
  on the Speakers page: APPEARANCE · WIDGET (what it shows; Developer Mode and Add the widget, with their
  state) · WINDOW (Close to tray, Start with Windows) · UPDATES (version,
  Check for updates, Check now / Update to x.y.z) ·
  HELP (Diagnostic log, Remove my data, Help add a speaker)
Speakers page (laid out like the home page): title bar keeps only "Tribit Connect" + window buttons ·
  stage: the selected speaker drawn large (XSound Plus 2 only; other models get their name, never a
  guessed shape) · exception word · note · one key (Open / Try again / Bluetooth settings / Help add it)
  bank: SPEAKERS legend · SUPPORTED and NOT SUPPORTED YET groups of latching keys lettered A, B, C… as on
  an amp's speaker selector (lamp = link) · dashed "+ pair" key (Bluetooth settings); scrolls past the width
```

Rejected before this: a "hardware rack" (a studio rack is not the retro-amp world, and it read as plain
boxes), then a Marantz-style tuning dial (crowded past four speakers, fake graduations, a 200px strip in an
empty page). The title bar hides the battery and link here: they belong to one speaker, and on this page
none is in use. SETTINGS stays: the app's own settings (updates above all) do not depend on a speaker.

**Speaker vs app settings: by page, with one SETTINGS key.** The Speakers page is the app's home, so its
SETTINGS holds the app's own settings; a speaker's page holds that speaker's, including its widget (the six
buttons and "Let the phone app connect" belong to the speaker, as the widget shows one speaker). Nothing
appears in both. Rejected (0.7.0): two sheets on one screen, one from the speaker's name and one from SETTINGS,
with a third set of keys on the Speakers page; the owner found it odd and not the split meant. The widget's
settings page holds only the speaker's settings (prompts, auto shutdown, release, speakers); the diagnostic
log is the app's and lives only on the Speakers page.

**Booting.** Until the host says the first connect attempt has settled (`speaker.settled`, capped at 4 s), only
the title bar ("Tribit Connect") shows. The launch decision is then made under that cover with no grace
period: linked → the instrument; not linked → the Speakers page at once. The instrument never flashes first.

**No "Reconnect".** The app never reconnects Bluetooth; it opens the speaker's controls over a link Windows
already has. So a missing link says why (`speaker.reason`, `Model.dropReason`): **off** ("Off or out of range",
Bluetooth settings), **busy** ("Controls busy", Try again: another controller such as the phone app holds it),
**bluetooth** (the radio), **released** (the app let go on purpose; no key). Try again appears only for busy.

Speakers page rules (owner's choices):

- **Shown only when needed** (`Model.rackReason`): the speaker gone for 3 s or more (`RACK_GRACE`; shorter
  drops and the host's own recheck keep the instrument), or two supported speakers paired and none chosen
  yet (asked once; `chosen` in state.json). ‹ Speakers in the title bar opens it any time; Esc
  or Open on the speaker in use closes it. Once open for a missing speaker it stays until that speaker links. A hidden window never shows it, so the idle release
  ("Let the phone app connect", sleep) does not throw the user out.
- **Kick back on disconnect, return by itself.** The editor stays as it was underneath (every change is
  already kept), so the speaker coming back returns to the same program.
- **Tribit speakers only.** Unsupported models say "Not supported yet" and their key opens the
  add-your-speaker site with `?model=`. Two of one model go by their Windows names.
- Pressing a selector key (or the arrow keys) only selects; the key on the stage acts on it.
- The key order is fixed when the page opens (in use, supported, others) and never reshuffles while up.
- **Per speaker:** only "Let the phone app connect" (`speakers[address]` in state.json). The widget's
  buttons were per speaker in 0.7.1–0.7.6; from 0.8.0 they are the first six programs, for every speaker.
- **Remembered speakers.** The host keeps what each speaker last reported under `speakers` in state.json (by
  address); switching shows that speaker's own last-known values, and an off speaker says when it was last seen.

Key concepts (0.8.0; the owner asked for Bandwave's simpler model):

- **One list of programs.** Everything playable is a program: the user's profiles and the six factory
  presets, in one order the user arranges (`library.order`, keys of both kinds; `Model.programs`). Own
  profiles come before the factory ones by default, because most people do not use the presets. **The first
  twelve are the program bar; the first six are the widget's and the tray menu's buttons.** Nothing else
  chooses either: no FACTORY/YOURS groups, no "main six", no widget picker. Settings says so once
  (Speakers page → Widget); no other screen carries instructions.
- **Play**: the speaker plays a program now (preset id, or custom bands via 0x82).
- **The editor is live.** Its keys never grey out (they are physical keys): UNDO rests pressed in while there is nothing to undo and pops up when a change is made. What is on the plate is what the speaker plays, and every change is kept as it
  is made (`Store.setBands`, written 500 ms after the hand stops; the curve streams through the
  latest-wins queue). There is no Save, Revert, Audition or draft. The transport keeps the plate's own
  keys (Bandwave's popup menu was tried and rejected as foreign to this look): **COMPARE**, a latching key
  with a lamp (lit: the speaker plays the program as it was when it came onto the plate, and the curve as it
  is now stays dashed; pressed again or a fader moved: back to it as it is), **UNDO** (back to how it was;
  Ctrl+Z), then at the foot **DUPLICATE**, **FLAT** and **DELETE** (the last two not on factory programs). Changing a factory program makes a copy ("My XBass") and edits that.
  **DELETE takes two presses** (0.8.2): the first arms it (pressed in, red lamp, label CONFIRM; in the
  right-click menu, a red "Delete <name>?"), the second within 3 s deletes; any other press or key, or
  waiting, disarms it. Undo stays after. A dialog and press-and-hold were rejected. **"+ new"** starts a
  flat "New program" after the current one; DUPLICATE is the way to copy. **Esc** leaves the editor as
  DONE does, once anything on top (menu, Settings, library, a light's pick) is closed.
  Editing needs the speaker: EDIT says why when it is not connected. Lights holding the program are
  written again when the editor moves on (`syncLights`), so "Profile changed" only shows if that failed.
- **Assign**: writes what an EQ-button light selects (0x18, all three lights in one write). On the home
  page: press a light's key, then a program. Clicking the EQ button on the drawing plays the next light.
- No confirm dialogs, ever (`confirm()` was rejected as "app.tribit says…"). Delete offers Undo.
- No standing instruction text (owner, 11 Nov 2026: it takes away from the look).

## 3. Tokens (`app/styles/app.css` `:root` and `:root[data-theme=dark]`)

Colour has jobs. Do not introduce a colour without a job.

| Role | Token(s) | Job |
|---|---|---|
| Chassis | `--ivory` (light #ece8de / dark #22221f), `--well`, `--alu` (plate), `--paper` (sheet, cards) | surfaces |
| Ink | `--ink`, `--ink-2`, `--ink-3`, `--engrave` | text tiers; `--engrave` for engraved legends |
| Panel | `--panel`, `--bank-*` (ink, rule, led, add key, All key, `--bank-shade`) | the program bank only. Light: the deepest step of the ivory family (#d6cfbf). Dark: charcoal (#131313) |
| Keys | `--key`, `--key-user`, `--key-edge`, `--key-ink` | program keys stay light in both themes; `--key-ink` is fixed dark |
| Act / attention | `--act`, `--act-edge`, `--act-hi`, `--on-act` | primary action (EDIT/DONE, Open, Update), switches on, focus ring, armed and offer pulses, drop target, the low-battery arc, display OFF. Light: ink (#34312b, ivory lettering). Dark: orange (#ff5a14) |
| Lit lamps | `--lit`, `--lit-face`, `--lit-glow` | every lamp that is on (Settings chips, Compare, annunciators, `.led.orange`). Light: a warm bulb, radial #fff3cc → #f2b33d with a soft #f2b33d halo. Dark: flat orange |
| Playing | `--amber`, `--amber-face`, `--amber-glow` | playing LED on keys, writing, Playing on the speaker. Light: the same bulb as `--lit`, so every glow in the window is one colour. Dark: #ffb21a |
| Bands | `--band-low`, `--band-mid`, `--band-high` | fader caps and LOW/MID/HIGH brackets only. Light: slate #4f73a8, sage #5f8a5c, clay #b86a45. Dark: #2f6fe4, #2f9e5b, #e4572e |
| Alarm | `--alarm`, `--danger`, `--danger-hi`, `--danger-edge` | Delete armed (lamp, menu item), Remove my data, the window's close hover. Light: brick #b8432f. Dark: #e4572e |
| Compare | `--lcd-compare` | the dashed "as it was" curve on the display. Light: the display's ink at 55%. Dark: amber |
| Lights | `Speaker.LIGHT` ['#4a4b51','#f7f8fb','#3d8bff'], `WIRE` ['#9b9ca4','#f6f6f1','#4d95ff'] | the speaker's three EQ-button lights; live conduit core colour |
| LCD | `--lcd`, `--lcd-ink`, `--lcd-dim`, `--lcd-ghost`, `--lcd-grid/-curve/-wash/-dots/-glow`, `--lcd-shade/-deep`, `--lcd-bezel`, `--lcd-cell` | every display. Light: a backlit pale display (#dde3cf, ink #26301f), so the whole light window is one family. Dark: the green-on-black display (#22271e, ink #d5ecab) |
| Fixed | `--needle`, `--blue-glow`, `--bulb-hi`, `--lamp0/1/2-*`, `--dot-off`, `--fader-track`, `--cap-off`, `--key-drop` | one job each; rules never write a colour of their own (`app/tests/style.test.js`) |
| Relief | `--lip` (lit lower edge of recesses), `--tlip` (legend text shadow), `--plate-hi`, `--plate-edge`, `--groove` | the only depth vocabulary; dark theme inverts `--tlip` to a dark lip |
| Mechanical keys | `--mech-a/-b` (gradient), `--mech-edge`, `--mech-hi` | every `.mech`, `.chip`, `.pk`, `.wtile`, `.dv-seg` |
| Off states | `--led-off`, `--win-off`, `--sw-off`, `--bezel` | unlit lamps, switch track, lamp-key bezel |
| Motion | `--ease` cubic-bezier(.2,.75,.2,1), `--ease-exit` cubic-bezier(.4,0,1,1), `--t-press` 80 ms, `--t-fast` 120, `--t-norm` 220, `--t-slow` 360, `--rise` 8px, `--stagger` 24 ms, `--t-busy` 1 s, `--t-attn` 1.2 s, `--t-spin` 8 s | every duration and curve (§6) |
| Geometry | `--fh` clamp(96px, (100vh−452px)·.55, 240px) | fader travel follows window height |

Theme: `js/theme.js` runs in `<head>` before CSS paints; reads `localStorage['tribit-connect.theme']`
(`light|dark|device`), sets `html[data-theme]`, posts `{type:'theme',dark}` to the host (DWM dark frame,
WebView background; host persists `LocalState/theme` to avoid a startup flash). Changes cross-fade via
`:root.theming` for `--t-slow`. Never write a colour in a rule: add a token (to both blocks if it differs
between themes). Black and white shades for shadows and highlights are the only exception; `style.test.js`
enforces it.

Type: Archivo (variable width axis; `font-stretch` 80–125% does the condensed/extended work),
Doto (dot-matrix, LCD text only), DSEG7 Classic Bold as `Seg` (numeric readouts only). Engraved legends
use `.engr` (600, stretch 120%, 10.5px, .16em tracking, uppercase, `--engrave`, `0 1px 0 var(--tlip)`).
No other fonts. Fonts are OFL and shipped in `app/fonts/` with licences.

## 4. Components (inventory and rules)

- **Key family** (`.mech`, `.chip`, `.pk`, `.wtile`, `.dv-seg`, `.pager`-adjacent): 1px `--mech-edge`
  border, 5px radius, `0 2px 0 var(--mech-edge)` drop, `inset 0 1px 0 var(--mech-hi)`, gradient
  `--mech-a→--mech-b`. `:active` translates 2px and removes the drop; a latched key (open, armed, nothing to undo, a state) looks
  exactly the same, with no reversed gradient or inner shadow, so pressing and latching never swap looks. Lamps are 6px dots via `::before`
  (`--win-off` off, `--lit-face` + `--lit-glow` on). Do not create a button outside this family.
- **Program key** (`.pkey`): 84px tall (72px under 700px window height), width clamp(62px,5.6vw,80px),
  number (Archivo 700 24px) + name; amber LED strip above (`.led`), light dots top-right
  (`.dot i.l0/l1/l2`), `.busy` = blinking LED while its write waits, `.playing` = amber LED. Every
  program key is `--key` (ivory), own and factory alike (0.8.2; the library's "≈ Factory" tag is the only
  difference). The key being edited has no outline (owner: removed in 0.8.2); its amber LED and the plate's
  name show it. Only a **visiting** key wears `--key-user`, the old faded tan, so it reads as a guest.
- **The bar's row** (`.userrow`, keys `.pkey.prog`): the first `M.BANK` (12) programs in the user's order
  (`bankProfiles`), numbered 01–12, with no groups or labels. `fit()` gives every key one width (`--kw`,
  56–112px) and hides whole keys from the end only when even 56px does not fit. Then `+ new`, an
  **editor-only** key that folds to width 0 on the home page (CSS transition, `inert`, never rebuilt), and
  the slim **All key** (`.pkey.lib`, 46px, "All N"). A program opened in the editor that is not on the bar
  **visits** as an extra key (`.visiting`) so the editor always has a lit key.
- **Key drag (pointer events, never HTML5 DnD)**: native DnD drew a translucent OS drag image next to the
  real key (rejected as "two images"). `drag` state: 6px threshold → the real key is `.held` (lifted,
  shadow) and slides **along the row only** (clamped to it; before 0.8.0 a key could be lifted off the bar
  and carried to a light, which read as a bug); the others get `translateX(±step)` with a .2s ease; drop
  settles the key (.18s) then `Store.move` to that place in the list. The click after a drag is swallowed
  (capture listener, removed next tick). Esc cancels. Alt+←/→ moves too. Right-click: the program's menu.
- **All programs** (`#library`, `ui.library`): a page, not a drawer. It spans grid rows 2–3, so it covers
  the stage **and** the program bar (the bar was wasted space under the old drawer), slides up
  (`translateY(calc(100% + 24px))` ↔ none, .38s, `visibility` delayed on close, `inert` while closed) while
  what is behind it fades. Close: DONE in its head, or Esc. One grid (`#libgrid`), six to a row: row one is
  the widget's six and rows one and two are the bar, which is why there are no section labels.
- **Records** (`.rec`): a **square** sleeve, like an album cover (owner: keep them square; all `--key`,
  factory ones with a small "≈ Factory" tag; key edge/drop, inset opening line) with number,
  `mini()` LCD curve, name, light dots; a vinyl disc (`<symbol id="disc">` in index.html, reused) peeks 16px
  out of the top, rises 8px on hover and 28px when playing (label takes `--disc-label` = amber), and spins
  (8 s/turn) only while `viz.listening && viz.playing`. Click plays (listen), opens (edit), or assigns
  (while a light is armed). Search filters by name/number; empty crates hide.
- **Record drag** (`ldrag`): `preventDefault` on pointerdown (no text selection); after 6px the record is
  `.held` and follows the pointer (`follow()`); **slots are measured once at drag start** (scroll-adjusted)
  and the target is the slot under the pointer (nearest centre otherwise). Testing against live, gliding
  cards made them oscillate. `place()` reorders and rebalances six-into-front with a two-container FLIP.
  Drop → `Store.move(key, slot)`. Disabled while searching.
- **Rolling names** (`rollName(box,on)`): only on the selected item (playing/editing key or record, lit
  light module) and only if it overflows: two copies in `.roll`, translateX(−width) at ~30 px/s, looping;
  reduced motion → static.
- **Ghost curve** (amber dashes): only while COMPARE is lit, the program as it is now, under the
  curve as it was (which is what the speaker plays then). The editor is live, so there is no other ghost.
- **Arming a light** sets `.app.choosing`: bank keys and library sleeves pulse (`offer`).
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
- **Settings sheet**: on a speaker's page, one section (name, facts, beep chips, auto shutdown, Let the phone
  app connect) and "App settings ›"; on the Speakers page, Appearance (segmented), Widget (one line on what it
  shows, then two numbered steps with their state), Window, Updates, Help. When it changes what it holds it
  fades the new contents in (`.swap`). The widget's 3×2 picker was removed in 0.8.0. Switch rows: bold label + one-line hint in
  plain language (never implementation detail such as "15 s after the widget hides"). Sheet is updated with
  `morph()` (attribute/text diff), never `innerHTML`, so switches slide and lamps fade.
- **Title bar**: the mini speaker appears left of the meter in edit mode so the right-hand cluster
  (meter, link, SETTINGS, window buttons) never shifts. Link word is `Linked` throughout writes
  (writes show on the key/module being written; the bulb flashing "Writing" was rejected).
- **Offline**: `.app.offline` greys speaker/lamps/meter/keys, conduits fade, offline card (engraved title,
  plain cause from `Model.dropReason`, Try again only for busy controls, else Bluetooth settings) rises over
  the speaker during the grace period; the Speakers page takes over after it.

## 5. State model (frontend)

- `js/model.js`: pure, unit-tested (`node --test app/tests/model.test.js`). Library
  `{profiles:[{key,name,bands,rev,basedOn,approx}], order:[keys, own and factory], links:{custom,slots[3]},
  widgetSlots}` (0.8.0; `normalizeLibrary` drops old `draft`, `widget` and `widgetBy` and builds `order`).
  `activeKey` identifies the speaker's custom curve by band content. `slotView` → synced/stale/changed/unknown.
  `programs`/`orderOf`/`numberOf`, `moveProfile`, `bankProfiles`/`BANK` (12), `WIDGET` (6), `uniqueName` (copies named after the original, numbered, ≤ `NAME_MAX` 32), `createProfile({from,after})` (inserts after its source), `widgetSlots` (resolved for the host: factory `{preset}`, saved
  `{preset:254,name,bands}`).
- `js/store.js`: the only writer. `persist()` sends `saveLibrary` with `widgetSlots` (the first six programs)
  attached; `setBands` (the live editor) persists debounced 500 ms. Curve stream: single-flight, latest wins;
  `apply` and `assign` run one at a time (`queue`). `Bridge.request` fails by itself after 15 s. `Store.transition(fn)`: `.leaving` 150 ms → `fn()` → `.arriving` 700 ms (timers, not frames,
  so a hidden window cannot stall it; instant under reduced motion).
- `js/app.js`: views. Rule: **build once, update in place.** `instruments()`, `speakerArt()`, `units()`,
  conduits, bank keys (`dataset.html` signature; playing/busy/editing toggled), sheet (`morph`). Only rebuild
  when structure truly changes.
- `js/bridge.js`: WebView2 `postMessage` or `MockBackend` (browser). Same JSON both ways; `request()`
  resolves by id with the host's result *after* the state it produced.

## 6. Motion rules

One system, calm and quick (since 0.9.0). Every duration and curve is a token in `:root`; rules never write
their own (`style.test.js` enforces it), and JS timers read the same tokens (`Store.motion`, `exitMs`, `enterMs`).

| Speed | Token | Used for |
|---|---|---|
| Press | `--t-press` 80 ms | a key or button going down |
| Fast | `--t-fast` 120 ms | hover, colour of controls, the hot fader column |
| Normal | `--t-norm` 220 ms | every exit; lamps, LEDs, switches, toast, menu, sheet close, the library closing |
| Slow | `--t-slow` 360 ms | every entrance; library opening, sheet opening, fader caps, folding widths, the theme cross-fade |
| Loops | `--t-busy` 1 s (blink), `--t-attn` 1.2 s (arming and offering pulse), `--t-spin` 8 s (a record) | status, never decoration |

- Entering uses `--ease` (decelerate); leaving uses `--ease-exit` (accelerate away). No bounce, no overshoot.
- One family of keyframes: `enter`, `exit` (opacity and `--rise`, 8 px), `fade-in`, `fade-out`. Direction is
  `--dir` on the element: 1 moves from/to below, −1 from/to above (the light modules, the Speakers keys,
  menus and the sheet). Centred elements keep their centring transform (`cardin`/`cardout`, the toast).
- A page change (home, editor, Speakers, and the bank swap): everything steps back together in `--t-norm`
  (`.leaving`, `Store.exitMs()` before the change), then the new page enters top to bottom, `--stagger`
  (24 ms) apart, starting with the title bar's name, back key, meter and link light (they change with the
  page); then modules, speaker, conduits; or name bar, plate, display, transport, then channels by `--i`.
  Every element that appears must also have a way out (offline card, sheet, toast, drawer).
- The window: a hidden window is shown cloaked (`WebViewHost::show`), the page answers `reveal` with `painted`
  once it has drawn, and only then is it uncloaked (1.5 s fallback), so there is no frame of bare panel colour.
  Back from the tray, the page enters as a page change does; the first opening lifts the cover. Closing to
  the tray and minimising keep Windows' own animations.
- The bank's program display folds as a shutter: its SVG has a fixed width, so nothing relayouts and the
  spectrum bars keep running while it reopens.
- Library crates share one 6-column grid that fills the drawer; record type scales with `cqw`.
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
Several speakers: the Settings page's "Speakers" row opens a Speakers page (up to three paired supported
speakers, `speaker.choose.N`, then Device details and Pair a speaker); Home shows **Switch** next to the status
when the speaker has no link and another is paired, and **Try again** only for busy controls (`can_try_again`).
The template never depends on what is paired (rows hide); `AllowMultiple` stays false: one controller link.

## 9. Verification workflow (mandatory for visual changes)

1. Iterate in the browser mock: open `app/index.html` (file:// works). Hash/query helpers: `#edit`,
   `#settings` (opens SETTINGS once launch settles; `?offline#settings` shows the app's), `?offline`, `?speakers=two|six|none`, `?uptodate`, `?music` (fake spectrum), `?many` (full YOURS row), `?debug` or backtick for the
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
- The editor is live: the program on the plate is the one the speaker plays, and every change is kept.
- Factory programs are read-only; their curves are approximations (≈).
- The visualizer analyses only this PC's audio to the default output when that output is the speaker;
  it never claims to know about phone playback.
- No confirm dialogs, no redundant text, no texture, no fades over controls, no OS drag images.
- User data lives in `%LOCALAPPDATA%\Tribit Connect` (outside the package; manifest disables file-write
  virtualization via `desktop6:FileSystemWriteVirtualization` + `unvirtualizedResources`). Nothing deletes it
  except Settings > Remove my data (inline two-step, `removeData` message). `data_folder()` migrates the old
  package LocalState once. MSIX uninstall cannot prompt; the README says so.
- Instrument rules (0.8.2–0.8.3): no tooltips anywhere (no `title` attributes; a name a screen reader
  needs goes in `aria-label`), and no mini speaker in the title bar (removed in 0.8.3: it did nothing);
  sideways legends only fade, never move, since a moving animation would replace their rotation;
  nothing is selectable but text fields (`body{user-select:none}`); the orange
  focus ring shows only after Tab (`.kbd` on the root, cleared by any pointer press), never on a clicked
  key after some other key press; the speaker drawing is a control only on the home page (`.spk-eq[tabindex]`),
  elsewhere a picture with its EQ ring unlit; the window's minimum size adds its resize borders so the page
  always gets its 960 × 620 (it used to overflow on the right).
- Hardening rules (0.7.4):
  - A page that hides the others (`.app.racked`, the Speakers page) does so with `display:none!important`.
    Mode rules of equal weight (`.app.editing .surface{display:grid}`) otherwise win by source order.
  - Every request gets a result. The host answers even a message it could not parse (`web_message` wraps
    `handle_message`), and `Bridge.request` fails by itself after 15 s.
  - Speaker writes from the page run one at a time (`store.js` `queue`); an assign is built from what the
    speaker reported after the write before it.
  - A command the app refuses (`Rejected` in `controller.hpp`: bad preset, band out of range, controls not
    open) fails alone. Only a real link failure resets the link.
  - The library reaches the page once, with the reply to `ready`, and passes through
    `Model.normalizeLibrary`. A `profiles.json` that exists but cannot be read is copied to
    `profiles.bad-<time>.json` and reported as `libraryError`; the page then saves nothing until the user
    changes something. `profiles.bak` is the library as it was at the session's first save.
  - `Core.transition` never drops a change: a new transition runs the waiting one first.
  - Setup's elevated step trusts only the certificate embedded in setup itself, never a file path, and a
    downloaded Windows App Runtime installer runs only if Windows trusts its signature.
  - The updater holds the verified setup open without write sharing until it is launched, and every
    waiting state (Checking, Downloading, Installing) ends by itself.
  - In the tray the speaker is always let go 15 s after the window hides (`Controller::release_always`).

## 11. Distribution

**Updates** (`src/updates.hpp`, ported from Bandwave): once a day while "Check for updates" is on (and on
Check now) the app reads this repository's latest GitHub release. It offers a version only if it is newer and
carries `TribitConnect-Setup-<x.y.z>.exe` and its `.sha256`; the download must match the hash, have an intact
signature and be signed by CN=TribitConnect.Local (thumbprint in `updates.hpp`). It then runs setup `/update`:
only the progress window, the package updates in place (data kept), and the app reopens. `package.ps1` writes
the versioned setup and its `.sha256` to `out/`; publishing the release is a separate, owner-approved step.
Installs from 0.4.0 and earlier have no updater and need one manual install. Published so far: v0.7.3
(10 Nov 2026, the first with the updater) and v0.8.3 (11 Nov 2026, the first release the updater can offer).
Each release carries the same five files: the setup, the versioned setup and `.sha256`, the MSIX and the `.cer`.

**Tray and startup** (0.7.3, ported from Bandwave). Close to tray (`state.json` `tray`, default off): the
window's WM_CLOSE hides it instead (`WebViewHost::on_close`), the WebView is set invisible so the page sees
`document.hidden`, the spectrum stops and `interest()` counts a hidden window as closed. From 0.7.4 the
speaker is released 15 s later whatever "Let the phone app connect" says, and showing the window again
decides which page to show as at launch (under the boot cover, after a short hold for the reconnect). The icon lives on a hidden `TribitConnect.Tray` window (message-only windows
miss `TaskbarCreated` and can't own the menu); it shows while the window is open with the switch on, and
always while the window is hidden. Click opens, and `WebViewHost::front()` makes sure the window really
comes forward (Windows can refuse `SetForegroundWindow` while the taskbar or the tray overflow has focus;
`front()` then borrows the foreground thread's input, and runs again 250 ms later). `front()` also always
raises the window explicitly (topmost, then not topmost): a window closed with its own X is hidden while it
is still its thread's active window, so on the next show Windows skips the activation that would raise it,
and it took the focus while staying under the window in front (0.7.6; `hide()` now clears the active window). The menu (0.7.5) shows
the speaker and battery, the widget's six buttons with the playing one ticked, Open and Quit. Opening the
menu keeps the controls open for 25 s (`tray_until`), and a button picked before the link is up plays once
it is (`tray_play`). Page additions in 0.7.5: the back key became "‹ <speaker>" on a Speakers page opened
by hand (removed in 0.8.2: the only way into a speaker is its card and Open, which always lands on its home
page, never the editor; going to the Speakers page leaves the editor as DONE does); Enter or a double-click on a selector key opens that speaker; a
speaker's Settings ends with "App settings ›"; every program key with a queued write blinks
(`ui.pending` is a set). Start with Windows is the
package's `windows.startupTask` (`TribitConnectStartup`, off by default). Windows owns its state, so the host
reads or requests it off the UI thread and reports `settings.startup`: `on`, `off`, `user-off` (turned off in
Settings > Apps > Startup, so only Windows can turn it back on), `policy-on`/`policy-off`, or `unavailable`
(an unpackaged build). Launched by the task (`AppInstance::GetActivatedEventArgs` is `StartupTask`), the app
starts in the tray.

`TribitConnect-Setup.exe` (`src/setup.cpp`, built by `scripts/package.ps1` after the MSIX it embeds as
RCDATA 101/102): TaskDialog progress → if the cert is not in LocalMachine\TrustedPeople, relaunch self
elevated with `--trust <cer>` (the only admin step) → if `Microsoft.WindowsAppRuntime.2` x64 ≥ 2.5.1 is
missing, download Microsoft's pinned installer (aka.ms/windowsappsdk/2.5/2.5.1/…-x64.exe) and run `--quiet`
→ `PackageManager.AddPackageAsync` (ForceApplicationShutdown | ForceUpdateFromAnyVersion) → offer Open
(`shell:AppsFolder\Kanu.TribitConnect_3k1b1c9z5y0mg!App`). `--check <file>` reports without changing
anything. Release builds stage in `out/release`; `out/package` is the dev registration (Windows keeps its
resources.pri mapped). SmartScreen still warns for the self-signed setup; the real fix is Store or trusted signing.
