// A scripted run-through of the page against the simulated speaker: open index.html?smoke in a
// browser. It presses the real controls and lists what held and what did not, in a box on the page.
// It is loaded only by mock-backend.js, and only with ?smoke; the app never loads it.
(async function(){
'use strict';
const $=id=>document.getElementById(id),wait=ms=>new Promise(r=>setTimeout(r,ms)),out=[];
const check=(name,ok,detail='')=>out.push(`${ok?'PASS':'FAIL'}  ${name}${ok||!detail?'':'  ('+detail+')'}`);
const click=el=>{if(!el)throw new Error('nothing to click');el.dispatchEvent(new MouseEvent('click',{bubbles:true,cancelable:true}));};
const key=(el,k,o={})=>el.dispatchEvent(new KeyboardEvent('keydown',{key:k,bubbles:true,cancelable:true,...o}));
const q=s=>document.querySelector(s),all=s=>[...document.querySelectorAll(s)];
const S=()=>window.Store,playing=()=>S().find(S().active())?.name;
try{
  for(let i=0;i<80&&(!S().settled||$('app').classList.contains('booting'));i++)await wait(100);
  check('the page opens on the speaker',!$('app').classList.contains('booting')&&$('rack').hidden);
  const programs=S().programs().length;
  check('the bar shows the first programs, numbered from 01',q('#keys .pkey.prog .n')?.textContent==='01'&&all('#keys .pkey.prog').length===Math.min(12,programs),`${all('#keys .pkey.prog').length} keys`);
  check('no FACTORY or YOURS groups on the bar',!q('#keys .group-label'));

  // play the second program
  const second=S().programs()[1];click(q(`#keys [data-key="${second.key}"]`));await wait(900);
  check('pressing a key plays it',playing()===second.name,playing());

  // the library: every program, one grid, square covers
  click(q('#keys [data-library]'));await wait(600);
  const recs=all('#libgrid .rec'),sleeve=recs[0]?.querySelector('.sleeve').getBoundingClientRect();
  check('All opens a page with every program',$('library').classList.contains('open')&&recs.length===programs,`${recs.length} of ${programs}`);
  check('covers are square',!!sleeve&&Math.abs(sleeve.width/sleeve.height-1)<.06,sleeve?`${sleeve.width.toFixed(0)}x${sleeve.height.toFixed(0)}`:'');
  check('six to a row',recs.length<7||Math.abs(recs[6].getBoundingClientRect().left-recs[0].getBoundingClientRect().left)<2);
  check('the library covers the program bar',$('library').getBoundingClientRect().bottom>$('bank').getBoundingClientRect().top+40);
  check('no lights or main-six sections, no hints',!q('#library .crate-label')&&!q('#library .hint'));
  click(q('#library [data-library]'));await wait(600);
  check('Done closes it',!$('library').classList.contains('open'));

  // the editor is live
  click($('editKey'));await wait(900);
  check('EDIT opens the editor',$('app').classList.contains('editing'));
  check('transport: Compare, Undo, Duplicate, Flat, Delete; no Save or More',['compare','undo','dup','flat','del'].every(k=>q(`[data-do=${k}]`))&&!q('[data-do=save]')&&!q('[data-do=more]'));
  check('no key on the plate is greyed out',!all('#transport .mech').some(b=>b.disabled));
  check('Undo rests pressed in with nothing to undo',q('[data-do=undo]').classList.contains('sunk'));
  const ed=()=>S().find(q('#keys .pkey.editing').dataset.key),before=ed().bands.slice();
  const slot=q('#channels [data-band="0"]');slot.focus();key(slot,before[0]<8?'ArrowUp':'ArrowDown');await wait(900);
  check('a fader move is kept at once',ed().bands[0]!==before[0]);
  check('and Undo pops up',!q('[data-do=undo]').classList.contains('sunk'));
  check('and the speaker plays it',S().speaker.bands?.[0]===ed().bands[0]&&S().speaker.preset===0xFE,JSON.stringify(S().speaker.bands));
  const nowBands=ed().bands.slice();click(q('[data-do=compare]'));await wait(900);
  check('Compare latches with its lamp and plays it as it was',q('[data-do=compare]').getAttribute('aria-pressed')==='true'&&S().speaker.bands?.[0]===before[0]);
  click(q('[data-do=compare]'));await wait(900);
  check('pressed again, it plays it as it is',q('[data-do=compare]').getAttribute('aria-pressed')==='false'&&S().speaker.bands?.[0]===nowBands[0]);
  click(q('[data-do=undo]'));await wait(900);
  check('Undo changes puts it back, on the speaker too',ed().bands[0]===before[0]&&S().speaker.bands?.[0]===before[0]);
  check('and Undo goes back in',q('[data-do=undo]').classList.contains('sunk'));

  // a factory program: the first change makes a copy
  // (picked from the library: with many own programs the factory ones are not on the bar)
  const factory=S().programs().find(p=>p.factory);click(q('#keys [data-library]'));await wait(600);
  click(q(`#libgrid [data-lkey="${factory.key}"]`));await wait(1100);
  check('picking a record in the editor closes the library',!$('library').classList.contains('open'));
  check('a factory program opens in the editor',ed().key===factory.key&&playing()===factory.name,playing());
  const s2=q('#channels [data-band="3"]');s2.focus();key(s2,'ArrowUp');await wait(900);
  check('changing it makes a copy and edits that',S().programs().length===programs+1&&ed().name==='My '+factory.name&&!ed().factory,ed().name);
  click(q('[data-do=del]'));await wait(400);
  check('Delete removes the copy and moves on',S().programs().length===programs&&!!q('#keys .pkey.editing'));
  click($('editKey'));await wait(900);
  check('DONE returns to the speaker',!$('app').classList.contains('editing'));

  // the EQ button on the drawing
  const lit=S().lit();click(q('#spk .spk-eq'));await wait(900);
  check('the EQ button steps to the next light',S().lit()===(lit+1)%3,`${lit} -> ${S().lit()}`);

  // settings: the widget's explanation lives on the Speakers page
  click($('deviceBtn'));await wait(300);
  check("a speaker's Settings has no widget picker",!q('#sheet .dv-wgrid')&&!!q('#sheet [data-dv=appsettings]'));
  click(q('#sheet [data-dv=appsettings]'));await wait(1200);
  check('App settings opens the Speakers page with Settings',!$('rack').hidden&&!$('sheet').hidden);
  check('which says what the widget and the bar show',/first six programs/.test($('sheet').textContent)&&/first twelve/.test($('sheet').textContent)&&all('#sheet .step').length===2);
  check('the editor is not behind the Speakers page',getComputedStyle($('surface')).display==='none');
}catch(e){out.push('ERROR  '+(e&&e.stack||e));}
const box=document.createElement('pre');box.id='smoke';box.textContent=out.join('\n');
box.style.cssText='position:fixed;inset:10px auto auto 10px;z-index:100;max-width:92vw;max-height:94vh;overflow:auto;background:#111;color:#e8e8e8;font:13px/1.5 Consolas,monospace;padding:12px 14px;border-radius:8px;white-space:pre-wrap';
document.body.append(box);
})();
