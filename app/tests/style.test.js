// The stylesheet keeps one palette and one motion system (docs/DESIGN.md §3 and §6): colours and timings
// live in the token blocks at the top of app.css, never in the rules.
const test=require('node:test');const assert=require('node:assert');
const fs=require('fs');const path=require('path');
const css=fs.readFileSync(path.join(__dirname,'../styles/app.css'),'utf8');
// Everything after the two theme blocks, without the simulator's own strip (not part of the app).
const rules=css.slice(css.indexOf('}',css.indexOf('color-scheme:dark;'))+1).replace(/\.mock-strip[^}]*\}/g,'');

test('colours come from tokens (black or white shades for shadows and highlights are fine)',()=>{
  const neutral=/^#(0{3,6}|f{3,6})([0-9a-f]{1,2})?$/i;
  const raw=(rules.match(/#[0-9a-fA-F]{3,8}\b/g)||[]).filter(h=>!neutral.test(h));
  assert.deepStrictEqual(raw,[]);
});

test('durations come from the motion tokens',()=>{
  const timed=(rules.match(/(transition|animation)(-delay|-duration)?\s*:[^;{}]*/g)||[])
    .filter(d=>/(^|[\s,(])\.?\d*\.?\d+m?s\b/.test(d.replace(/var\([^)]*\)/g,'').replace(/\b0s\b/g,'')));   // 0s: an instant switch
  assert.deepStrictEqual(timed,[]);
});

test('both themes define the same colour tokens for the bar and the displays',()=>{
  const block=sel=>{const i=css.indexOf(sel);return css.slice(i,css.indexOf('}',i));};
  const names=b=>new Set((b.match(/--[\w-]+(?=:)/g)||[]));
  const light=names(block(':root{')),dark=names(block(':root[data-theme=dark]{'));
  for(const n of light)if(/^--(bank|lcd)/.test(n))assert.ok(dark.has(n),n+' is missing from the dark theme');
});
