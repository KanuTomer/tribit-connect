// Sound Lab. Two modes on one instrument:
//   listening – assignment modules for the three EQ-button lights, the speaker, the program bank
//   editing   – the bank's display unfolds into a fader plate whose columns line up with the curve
// Saving (this PC), applying (plays the saved version) and assigning (writes the button's
// lights) stay separate. The speaker only ever plays an unsaved draft while AUDITION is lit.
(function(){
'use strict';
const S=Store,M=S.M,B=M.BANDS,N=B.freqs.length,$=id=>document.getElementById(id),esc=S.esc;
const region=i=>i<3?'var(--low)':i<6?'var(--mid)':'var(--high)';
const ui={mode:'listen',edit:null,draft:null,choosing:null,hot:-1,toast:null,pending:null,wpick:null,lastPick:null,library:false,removing:false};
const aud={on:false,before:null};   // audition: what the speaker played before it started

// ---- curves by band position --------------------------------------------------------------------
// The editor's display is laid out in band columns, so each band's point sits above its fader.
// Factory shapes are traced evenly between the lowest and highest band, which maps straight on.
function curveByBand(p,bands,count=200){
  return Array.from({length:count},(_,k)=>{
    const x=Math.max(0,Math.min(N-1,-.5+k/(count-1)*N));
    if(p.factory&&!bands){const t=p.trace,pos=x/(N-1)*(t.length-1),j=Math.min(Math.floor(pos),t.length-2),u=pos-j;return ((t[j]*(1-u)+t[j+1]*u)-p.base)/38;}
    const b=bands||p.bands,i=Math.min(Math.floor(x),N-2),u=x-i,p0=b[Math.max(i-1,0)],p1=b[i],p2=b[i+1],p3=b[Math.min(i+2,N-1)];
    return .5*(2*p1+(-p0+p2)*u+(2*p0-5*p1+4*p2-p3)*u*u+(-p0+3*p1-3*p2+p3)*u*u*u);
  });
}
// ---- displays: a pen draws each new curve; fader moves redraw at once -------------------------
function plotter(svg,{pad,labels,inset=0}){
  const P={svg,pad,cur:null,raf:0,w:0,h:0,inset};
  // X spans band columns: column i is centred at (i+.5)/N of the channel width. `inset` is the
  // display's offset inside that width (its bezel), so the columns line up with the faders.
  P.layout=()=>{const r=svg.getBoundingClientRect();if(!r.width||(r.width===P.w&&r.height===P.h))return;P.w=r.width;P.h=r.height;svg.setAttribute('viewBox',`0 0 ${P.w} ${P.h}`);
    const span=P.w+2*inset;P.CX=i=>(i+.5)/N*span-inset;P.X=k=>k*P.w;
    P.Y=d=>pad[0]+(1-(d+9)/18)*(P.h-pad[0]-pad[2]);P.DB=y=>Math.round(9-(y-pad[0])/(P.h-pad[0]-pad[2])*18);
    let g='';
    if(labels){for(let i=0;i<N;i++)g+=`<line x1="${P.CX(i)}" x2="${P.CX(i)}" y1="${pad[0]-6}" y2="${P.h-pad[2]+4}"/>`;
      for(const d of [-8,-4,0,4,8])g+=`<line class="${d===0?'zero':''}" x1="0" x2="${P.w}" y1="${P.Y(d)}" y2="${P.Y(d)}"/><text x="6" y="${P.Y(d)-3}">${d>0?'+'+d:d}</text>`;}
    else g+=`<line class="zero" x1="0" x2="${P.w}" y1="${P.Y(0)}" y2="${P.Y(0)}"/>`;
    svg.innerHTML=`<rect class="col-hot" y="0" height="${P.h}" width="${span/N}" x="-999"/><g class="bars">${Array.from({length:N},()=>'<rect rx="1.5"/>').join('')}</g><g class="grid">${g}</g><path class="curve-fill"/><path class="ghost"/><path class="curve"/><g class="marks"></g><circle class="nib" r="3.5"/>`;
    // Redraw everything this display owns from cached state; never re-render the page from here.
    if(P.cur)P.draw(P.cur);P.ghost(P.lastGhost);P.marks(P.lastMarks);P.hot(P.lastHot??-1);};
  P.path=v=>v.map((d,k)=>(k?'L':'M')+P.X(k/(v.length-1)).toFixed(1)+' '+P.Y(d).toFixed(1)).join('');
  P.draw=v=>{if(!P.w)return;const c=svg.querySelector('.curve');c.setAttribute('d',P.path(v));c.style.strokeDasharray='';c.style.strokeDashoffset='';
    svg.querySelector('.curve-fill').setAttribute('d',P.path(v)+`L${P.w} ${P.h}L0 ${P.h}Z`);};
  P.pen=v=>{
    cancelAnimationFrame(P.raf);clearTimeout(P.stall);P.cur=v;if(!P.w)P.layout();if(!P.w)return;
    P.draw(v);if(S.reduce())return;
    const c=svg.querySelector('.curve'),fill=svg.querySelector('.curve-fill'),nib=svg.querySelector('.nib'),len=c.getTotalLength(),t0=performance.now(),T=700;
    const finish=()=>{cancelAnimationFrame(P.raf);clearTimeout(P.stall);c.style.strokeDasharray='';c.style.strokeDashoffset='';fill.style.opacity=1;nib.style.opacity=0;};
    c.style.strokeDasharray=len;c.style.strokeDashoffset=len;fill.style.opacity=0;nib.style.opacity=1;
    const step=t=>{const k=Math.min(1,(t-t0)/T),e=1-Math.pow(1-k,3),at=c.getPointAtLength(len*e);
      c.style.strokeDashoffset=len*(1-e);nib.setAttribute('cx',at.x);nib.setAttribute('cy',at.y);
      if(k<1)P.raf=requestAnimationFrame(step);else finish();};
    // Frames stop while the window is hidden; never leave a half-drawn curve behind.
    P.stall=setTimeout(finish,T+250);P.raf=requestAnimationFrame(step);};
  P.set=v=>{cancelAnimationFrame(P.raf);clearTimeout(P.stall);P.cur=v;P.draw(v);const n=svg.querySelector('.nib');n&&(n.style.opacity=0);const f=svg.querySelector('.curve-fill');f&&(f.style.opacity=1);};
  P.ghost=v=>{P.lastGhost=v;const g=svg.querySelector('.ghost');g&&g.setAttribute('d',v&&P.w?P.path(v):'');};
  P.marks=b=>{P.lastMarks=b;const g=svg.querySelector('.marks');if(!g)return;g.innerHTML=b&&P.w?b.map((d,i)=>`<rect class="mark" x="${P.CX(i)-3.5}" y="${P.Y(d)-3.5}" width="7" height="7"/>`).join(''):'';};
  P.hot=i=>{P.lastHot=i;const r=svg.querySelector('.col-hot');if(r&&P.w)r.setAttribute('x',i<0?-999:P.CX(i)-(P.w+2*inset)/N/2);};
  new ResizeObserver(()=>P.layout()).observe(svg);
  return P;
}
const lplot=plotter($('lplot'),{pad:[14,8,8,8],labels:false}),dplot=plotter($('dplot'),{pad:[14,10,14,8],labels:true,inset:5});
let lastLcd=null,lastDisplay=null;

// ---- the LCD's spectrum ------------------------------------------------------------------------------
// What this PC is playing through the speaker (the host listens to its own output), drawn as faint
// bars in the band columns behind the curve. Rises quickly, falls slowly, and stops when silent.
const viz={target:Array(N).fill(0),cur:Array(N).fill(0),raf:0,playing:false,listening:false};
Bridge.on(m=>{
  if(m.type!=='spectrum')return;
  const was=viz.playing&&viz.listening;viz.listening=!!m.listening;viz.playing=!!m.playing;
  if(was!==(viz.playing&&viz.listening)&&S.ready())bank();
  viz.target=m.playing&&m.bands?.length===N?m.bands:Array(N).fill(0);
  if(!viz.raf&&!S.reduce())viz.raf=requestAnimationFrame(vizStep);
});
function vizStep(){
  let moving=false;
  for(let i=0;i<N;i++){const t=viz.target[i]||0,c=viz.cur[i];viz.cur[i]=c+(t-c)*(t>c?.35:.08);if(viz.cur[i]>.003)moving=true;}
  drawBars();viz.raf=moving||viz.playing?requestAnimationFrame(vizStep):0;
}
function drawBars(){
  const rects=$('lplot').querySelectorAll('.bars rect');if(rects.length!==N||!lplot.w)return;
  const w=lplot.w/N*.46,base=lplot.h-4,top=14;
  rects.forEach((r,i)=>{const h=Math.max(0,viz.cur[i]*(base-top));r.setAttribute('x',lplot.CX(i)-w/2);r.setAttribute('y',base-h);r.setAttribute('width',w);r.setAttribute('height',h);});
}

// ---- what is shown ------------------------------------------------------------------------------
const playing=()=>S.find(S.active());
const editing=()=>ui.edit?S.find(ui.edit):null;
const editBands=()=>{const p=editing();return p?(ui.draft||(p.factory?S.bandsOf(p):p.bands)):null;};
const pad2=n=>n==null?'··':String(n).padStart(2,'0');
const lightsOf=key=>S.slots().map((v,i)=>v.key===key?i:-1).filter(i=>i>=0);
const nameOf=p=>!p?'—':p.foreign?'Custom curve':p.name;
const hasDraft=p=>!p.factory&&(ui.mode==='edit'&&ui.edit===p.key?!!ui.draft:!!p.draft);
const draftPlaying=()=>!!ui.draft&&S.speaker.preset===M.CUSTOM&&M.sameBands(S.speaker.bands,ui.draft);

function instruments(){
  const s=S.speaker,v=Math.max(0,Math.min(100,s.battery??0)),known=S.linked()&&s.battery>=0;
  // Writes show on the key or module being written; the link light only reports the link itself.
  const [cls,word]=s.status==='connecting'?['busy','Connecting']:s.connected?['on','Linked']:s.status==='unavailable'?['err','Bluetooth unavailable']:['','Not connected'];
  const ang=-60+v*1.2,el=$('instruments');
  if(el.firstChild){
    const m=el.querySelector('.meter');m.setAttribute('aria-label',`Battery ${known?v+' percent':'unknown, not connected'}`);
    m.querySelector('.needle').style.transform=`rotate(${known?ang:-60}deg)`;m.querySelector('.pct').textContent=known?v+'%':'--';
    el.querySelector('.bulb').className='bulb '+cls;el.querySelector('.word').textContent=word;return;
  }
  el.innerHTML=`<svg class="meter" width="92" height="44" viewBox="0 0 92 44" role="img" aria-label="Battery ${known?v+' percent':'unknown, not connected'}">
      <rect class="face" x=".5" y=".5" width="91" height="43" rx="4"/>
      <path class="scale-arc" d="M18 30A32 32 0 0 1 74 30"/><path class="low-arc" d="M18.6 28.8A32 32 0 0 1 24.4 21"/>
      ${[0,25,50,75,100].map(t=>{const a=(-60+t*1.2)*Math.PI/180;return `<line x1="${46+Math.sin(a)*28}" y1="${36-Math.cos(a)*28}" x2="${46+Math.sin(a)*33}" y2="${36-Math.cos(a)*33}" stroke="var(--engrave)"/>`;}).join('')}
      <line class="needle" x1="46" y1="36" x2="46" y2="6" style="transform:rotate(${known?ang:-60}deg)"/><circle cx="46" cy="36" r="3" fill="var(--engrave)"/>
      <text x="6" y="40">BATT</text><text class="pct" x="86" y="40" text-anchor="end">${known?v+'%':'--'}</text></svg>
    <span class="linkind"><span class="bulb ${cls}"></span><span class="word">${word}</span></span>`;
}
function speakerArt(){
  const s=S.speaker,state=s.status==='connecting'?'connecting':s.connected?'linked':'off',lit=S.lit();
  if(!$('spk').firstChild){$('spk').innerHTML=Speaker.svg({light:lit,state,id:'big'});$('mini').innerHTML=Speaker.svg({light:lit,state,id:'mini',interactive:false});}
  for(const el of [$('spk'),$('mini')])Speaker.set(el,{light:lit,state});
}
// ---- the three assignment modules ---------------------------------------------------------------
function units(){
  const slots=S.slots(),lit=S.lit(),off=!S.linked(),box=$('units');
  if(!box.children.length)box.innerHTML=[0,1,2].map(i=>`<div class="unit" data-unit="${i}">
      <div class="head"><span class="engr">${Speaker.NAMES[i]}</span></div>
      <div class="body">
        <button class="lampkey" data-light="${i}" data-arm="${i}"><span class="face"></span></button>
        <button class="readout" data-arm="${i}" tabindex="-1" aria-hidden="true"><span class="num"><span></span></span><span class="nm"></span></button>
      </div>
      <div class="foot"></div></div>`).join('');
  slots.forEach((v,i)=>{
    const p=v.key?S.find(v.key):null,busy=S.busy==='assign:'+i,armed=ui.choosing===i;
    // Offline the modules show what was last read from the speaker, and say so.
    const [led,text]=off?['',v.status==='unknown'?'Not read yet':'Last known']:armed?['orange blink','Choose a program ↓']:busy?['amber blink','Writing…']:
      v.status==='stale'?['orange','Profile changed']:v.status==='changed'?['orange','Set elsewhere']:
      v.status==='unknown'?['','Not read yet']:[null,''];
    const u=box.children[i],key=u.querySelector('.lampkey'),ro=u.querySelector('.readout');
    u.classList.toggle('lit',lit===i);u.classList.toggle('armed',armed);
    key.disabled=ro.disabled=off;key.setAttribute('aria-pressed',armed);
    key.setAttribute('aria-label',`Change what the ${Speaker.NAMES[i].toLowerCase()} selects (now ${nameOf(p)})`);
    ro.querySelector('.num span').textContent=pad2(v.key?S.number(v.key):null);const nm=ro.querySelector('.nm');
    if(nm.dataset.name!==nameOf(p)){nm.dataset.name=nameOf(p);nm.dataset.roll='';nm.textContent=nameOf(p);}rollName(nm,lit===i);
    const foot=`${led===null?'':`<span class="led ${led}"></span><span class="engr">${text}</span>`}${v.status==='stale'&&!busy&&!off?`<button class="mech" data-rewrite="${i}">Rewrite</button>`:''}`;
    const f=u.querySelector('.foot');if(f.dataset.html!==foot){f.dataset.html=foot;f.innerHTML=foot;}
  });
  requestAnimationFrame(conduits);
}
// Over the speaker while it is not connected: what is wrong and the two ways to fix it.
function offlineCard(){
  const s=S.speaker,el=$('offline'),connecting=s.status==='connecting';
  // Linked again: the card fades out before it is hidden.
  if(S.linked()){if(!el.hidden&&!el.classList.contains('closing')){el.classList.add('closing');
    setTimeout(()=>{el.classList.remove('closing');if(S.linked())el.hidden=true;},S.reduce()?0:220);}return;}
  el.classList.remove('closing');el.hidden=false;
  const seen=s.lastSeen?new Date(s.lastSeen).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'}):'';
  const why=connecting?'Opening the control link…':s.status==='unavailable'&&s.error?esc(s.error):
    'Turn it on and check it is paired with this PC. It takes one controller at a time, so close the Tribit app on your phone.';
  el.innerHTML=`<span class="engr"><span class="led ${connecting?'amber blink':'orange'}"></span>${connecting?'Connecting':'Speaker not connected'}</span>
    <p>${why}${seen&&!connecting?`<br>Last seen ${seen}; the lights and programs show what was read then.`:''}</p>
    ${connecting?'':'<div class="acts"><button class="mech" data-dv="retry">Reconnect</button><button class="mech" data-dv="bt">Bluetooth settings</button></div>'}`;
}
// Engraved conduits from each module down to the EQ button; the playing light's is lit.
// Three wires ease down from the modules and meet the button's ring side by side; the playing
// light's wire carries that light's colour.
const WIRE=['#9b9ca4','#f6f6f1','#4d95ff'];
function conduits(){
  const rig=$('rig'),eq=rig.querySelector('.spk-eq .spk-ring');if(!eq||ui.mode!=='listen')return;
  // Everything is measured in the rig's own layout, ignoring the rise/fall transforms of a
  // mode change, so the wires never stretch while the parts settle.
  const W=rig.querySelector('.spkwrap'),Wr=W.getBoundingClientRect(),E=eq.getBoundingClientRect();if(!Wr.width)return;
  const cx=W.offsetLeft+E.left+E.width/2-Wr.left,cy=W.offsetTop+E.top+E.height/2-Wr.top,rr=E.width/2,lit=S.lit();let cut='',live='';
  // Each wire arrives along a radius and stops on the ring's outer edge, like a lead in a socket.
  const out=rr*1.09;
  rig.querySelectorAll('.unit').forEach((u,i)=>{const x=u.offsetLeft+u.offsetWidth/2,y=u.offsetTop+u.offsetHeight;
    const t=(i-1)*.5,sx=Math.sin(t),cy2=-Math.cos(t),ex=cx+out*sx,ey=cy+out*cy2,y0=y+8;
    const d=`M${x} ${y}V${y0}C${x} ${(y0+ey)/2} ${cx+(out+26)*sx} ${cy+(out+26)*cy2} ${ex} ${ey}`;
    cut+=`<path class="lip" d="${d}"/><path class="cut" d="${d}"/>`;
    live+=`<g class="wire" data-w="${i}"><path class="cut live" d="${d}"/><path class="glow" stroke="${WIRE[i]}" d="${d}"/><path class="core" stroke="${WIRE[i]}" d="${d}"/></g>`;});
  const svg=$('conduits'),html=cut+live;if(svg.dataset.html!==html){svg.dataset.html=html;svg.innerHTML=html;}
  svg.querySelectorAll('.wire').forEach(w=>w.classList.toggle('on',+w.dataset.w===lit));
}
// ---- program bank -----------------------------------------------------------------------------------
function bank(){
  const active=S.linked()?S.active():null,editKey=ui.mode==='edit'?ui.edit:null;
  const key=(p,cls)=>{const lights=lightsOf(p.key);return `<button class="pkey ${cls}" data-key="${p.key}">
    <span class="led"></span><span class="n">${pad2(S.number(p.key))}${hasDraft(p)?'<i class="dm" title="Unsaved draft"></i>':''}</span>${lights.length?`<span class="dot">${lights.map(i=>`<i class="l${i}"></i>`).join('')}</span>`:''}<span class="t" data-name="${esc(p.name)}">${esc(p.name)}</span></button>`;};
  // YOURS is the first six saved profiles (the library's bank zone). A profile being edited that is
  // not among them visits as a sixth key, so the editor always has a lit key. "+ new" is an editor
  // key: it folds away on the home page (CSS), so the row never rebuilds when the mode changes.
  const n=S.custom().length,onBank=M.bankProfiles(S.library),visit=editKey&&!S.find(editKey)?.factory&&!onBank.some(p=>p.key===editKey)?S.find(editKey):null;
  const html=`<div class="group factory"><span class="group-label">FACTORY</span><div class="row">${S.factory.map(p=>key(p,'')).join('')}</div></div>
    <div class="group user"><span class="group-label">YOURS</span><div class="row userrow">${onBank.map(p=>key(p,'user')).join('')}${visit?key(visit,'user visiting'):''}
    <button class="pkey add" data-new aria-label="New profile"><span class="n">+</span><span class="t">new</span></button>
    ${n?`<button class="pkey lib" data-library aria-controls="library"><svg width="22" height="16" viewBox="0 0 22 16" aria-hidden="true">${[0,8,16].map(x=>[0,9].map(y=>`<rect x="${x}" y="${y}" width="6" height="7" rx="1.2"/>`).join('')).join('')}</svg><span class="t lib-all">All ${n}</span><span class="t lib-close">Close</span></button>`:''}</div></div>`;
  if($('keys').dataset.html!==html&&!drag.started){$('keys').dataset.html=html;$('keys').innerHTML=html;}
  $('keys').querySelector('[data-new]').inert=ui.mode!=='edit';
  const lib=$('keys').querySelector('[data-library]');if(lib){lib.setAttribute('aria-expanded',ui.library);lib.setAttribute('aria-label',ui.library?'Close the library':`Library, ${n} profiles`);}
  $('keys').querySelectorAll('[data-key]').forEach(k=>{const p=S.find(k.dataset.key),on=active===p.key,ed=editKey===p.key;
    k.classList.toggle('playing',on);k.classList.toggle('busy',S.busy==='apply'&&ui.pending===p.key);k.classList.toggle('editing',ed);
    k.setAttribute('aria-label',`${pad2(S.number(p.key))} ${p.name}${on?', playing':''}${hasDraft(p)?', unsaved draft':''}`);
    rollName(k.querySelector('.t'),on||ed);});
  $('app').classList.toggle('choosing',ui.choosing!==null);
  fit($('keys').querySelector('.userrow'));
  const p=playing();
  $('lstate').textContent=!S.linked()?'OFFLINE':viz.listening&&!viz.playing?'PAUSED':'';$('lstate').classList.toggle('off',!S.linked());
  const id=p?p.key+':'+(p.bands||[]).join():'none';
  if(id!==lastLcd){lastLcd=id;lplot.pen(p?curveByBand(p):Array(200).fill(0));}
  $('editKey').setAttribute('aria-label',ui.mode==='edit'?'Done editing':'Edit sounds');
}
// Every program key shares one width (--kw), chosen so the keys fill the room the display and EDIT
// key leave; in the editor the display folds away, so they grow (the width transitions). The room is
// projected from the display's target width, so the keys move with it, not after it. When even the
// narrowest keys do not fit, whole saved-profile keys drop from the end; the Library has them all.
function fit(row){
  const keys=$('keys'),wrap=document.querySelector('.lcdwrap'),lcd=$('lcd'),user=[...row.querySelectorAll('.pkey.user')];
  const lcdW=ui.mode!=='edit'&&getComputedStyle(lcd).display!=='none'?Math.min(210,Math.max(150,innerWidth*.13))+14:0;
  const room=keys.clientWidth+wrap.offsetWidth-(lcdW+$('editKey').offsetWidth);
  const gap=parseFloat(getComputedStyle(row).columnGap)||8,more=ui.mode==='edit'?1:0,lib=row.querySelector('.pkey.lib')?46:0;
  const width=n=>(room-lib-10-gap*(5+n+more))/(6+n+more);
  let n=user.length;while(n>0&&width(n)<56)n--;
  user.forEach((k,i)=>k.hidden=i>=n);
  keys.style.setProperty('--kw',Math.max(56,Math.min(112,width(n))).toFixed(1)+'px');
}
// A selected name too long for its key scrolls round in a loop; unselected names stay clamped.
function rollName(box,on){
  if(!box)return;const text=box.dataset.name??box.textContent;
  if(!on||S.reduce()){if(box.dataset.roll){box.dataset.roll='';box.classList.remove('rolling');box.textContent=text;}return;}
  if(box.dataset.roll===text)return;
  box.classList.add('rolling');box.innerHTML=`<span class="roll"><span>${esc(text)}</span><span aria-hidden="true">${esc(text)}</span></span>`;
  const w=box.querySelector('.roll span').offsetWidth;
  if(w-24<=box.clientWidth){box.classList.remove('rolling');box.textContent=text;box.dataset.roll='';return;}
  box.dataset.roll=text;const r=box.querySelector('.roll');r.style.setProperty('--d',w+'px');r.style.setProperty('--dur',(w/30).toFixed(1)+'s');
}
// ---- library: a drawer of records under the bank ----------------------------------------------
// Every saved profile is a record: a sleeve (number, curve, name) with its disc peeking out. The
// first six sit in the front crate, ON THE BANK; dragging a record there puts it on the bank.
const MINI_GRID=`M0 7.5H100M0 22.5H100${[1,2,3,4,5,6,7,8].map(i=>`M${(i*100/9).toFixed(1)} 0V30`).join('')}`;
const mini=p=>{const v=curveByBand(p,null,48),line=v.map((d,k)=>(k?'L':'M')+(k/(v.length-1)*100).toFixed(1)+' '+(15-d*1.6).toFixed(1)).join('');
  return `<svg viewBox="0 0 100 30" preserveAspectRatio="none" aria-hidden="true"><path class="g" d="${MINI_GRID}"/><path class="z" d="M0 15H100"/>
    <path class="f" d="${line}L100 30L0 30Z"/><path class="glow" d="${line}"/><path class="c" d="${line}"/></svg>`;};
const record=p=>{const lights=lightsOf(p.key);return `<div class="rec" data-lkey="${p.key}" role="button" tabindex="0">
  <span class="discwrap" aria-hidden="true"><svg class="disc" viewBox="0 0 100 100"><use href="#disc"/></svg></span>
  <span class="sleeve"><span class="n">${pad2(S.number(p.key))}${hasDraft(p)?'<i class="dm" title="Unsaved draft"></i>':''}</span>
    <span class="mini">${mini(p)}</span><span class="t" data-name="${esc(p.name)}">${esc(p.name)}</span>
    ${lights.length?`<span class="dot">${lights.map(i=>`<i class="l${i}"></i>`).join('')}</span>`:''}</span></div>`;};
function library(){
  const el=$('library');el.classList.toggle('open',ui.library);el.inert=!ui.library;$('app').classList.toggle('lib-open',ui.library);
  if(!ui.library&&!el.dataset.drawn)return;
  const q=$('libsearch').value.trim().toLowerCase(),all=S.custom(),active=S.linked()?S.active():null;
  const front=all.slice(0,M.BANK).map(record).join(''),back=all.slice(M.BANK).map(record).join('');
  if((el.dataset.drawn!==front+back)&&!ldrag.started){el.dataset.drawn=front+back;$('libzone').innerHTML=front;$('librest').innerHTML=back;}
  let shown=0;
  el.querySelectorAll('[data-lkey]').forEach(c=>{const p=S.find(c.dataset.lkey);
    c.hidden=!!q&&!(p.name.toLowerCase().includes(q)||pad2(S.number(p.key)).includes(q));shown+=!c.hidden;
    const on=active===p.key,ed=ui.mode==='edit'&&ui.edit===p.key;
    c.classList.toggle('playing',on);c.classList.toggle('spinning',on&&viz.listening&&viz.playing);c.classList.toggle('editing',ed);
    rollName(c.querySelector('.t'),on||ed);});
  for(const crate of el.querySelectorAll('.crate'))crate.hidden=!!q&&!crate.querySelector('[data-lkey]:not([hidden])');
  el.querySelector('.crate.back').hidden||=!q&&all.length<=M.BANK;
  $('libcount').textContent=q?`${shown} of ${all.length}`:all.length;$('libempty').hidden=!q||shown>0;
}
function toggleLibrary(open){ui.library=open;$('libsearch').value='';render();}
$('libsearch').addEventListener('input',()=>library());
// The bank's display folds away in the editor; once it has, the YOURS row knows its real width.
$('lcd').addEventListener('transitionend',e=>{if(e.propertyName==='width')fit($('keys').querySelector('.userrow'));});
// Records are arranged by dragging (not while searching). Slots are measured once when the drag
// starts, and the target is the slot under the pointer: testing against cards that are still
// gliding made them swap back and forth. The front crate always holds the first six.
const ldrag={el:null,started:false,x0:0,y0:0,gx:0,gy:0,slots:[],at:-1,scroll0:0};
function flip(boxes,change,skip){
  const items=boxes.flatMap(b=>[...b.children]).filter(c=>c!==skip),before=new Map(items.map(c=>[c,c.getBoundingClientRect()]));
  change();if(S.reduce())return;
  items.forEach(c=>{const a=before.get(c),b=c.getBoundingClientRect(),dx=a.left-b.left,dy=a.top-b.top;if(!dx&&!dy)return;
    c.style.transition='none';c.style.transform=`translate(${dx}px,${dy}px)`;c.getBoundingClientRect();
    c.style.transition='transform .24s var(--ease)';c.style.transform='';});
}
function follow(x,y){const d=ldrag,el=d.el;el.style.transform='';const r=el.getBoundingClientRect();el.style.transform=`translate(${x-d.gx-r.left}px,${y-d.gy-r.top}px)`;}
// Puts the dragged record at list position `at`, keeping the bank's six in the front crate.
function place(at){
  const zone=$('libzone'),rest=$('librest'),el=ldrag.el,list=[...zone.children,...rest.children].filter(c=>c!==el);
  list.splice(at,0,el);flip([zone,rest],()=>{list.forEach((c,i)=>(i<M.BANK?zone:rest).appendChild(c));},el);
}
document.addEventListener('pointerdown',e=>{
  if(e.button!==0||ldrag.el)return;const c=e.target.closest('.rec[data-lkey]');if(!c)return;
  e.preventDefault();   // no text selection, no native drag
  if($('libsearch').value.trim())return;
  Object.assign(ldrag,{el:c,started:false,x0:e.clientX,y0:e.clientY});
});
document.addEventListener('pointermove',e=>{
  const d=ldrag;if(!d.el)return;
  const body=$('libbody');
  if(!d.started){if(Math.hypot(e.clientX-d.x0,e.clientY-d.y0)<6)return;d.started=true;
    const r=d.el.getBoundingClientRect();d.gx=d.x0-r.left;d.gy=d.y0-r.top;d.el.classList.add('held');d.scroll0=body.scrollTop;
    const recs=[...$('libzone').children,...$('librest').children];d.at=recs.indexOf(d.el);
    d.slots=recs.map(c=>{const b=c.getBoundingClientRect();return {l:b.left,r:b.right,t:b.top+body.scrollTop,b:b.bottom+body.scrollTop};});}
  e.preventDefault();const B=body.getBoundingClientRect();
  if(e.clientY<B.top+30)body.scrollTop-=12;else if(e.clientY>B.bottom-30)body.scrollTop+=12;
  const x=e.clientX,y=e.clientY+body.scrollTop;
  let at=d.slots.findIndex(s=>x>=s.l&&x<=s.r&&y>=s.t&&y<=s.b);
  if(at<0){let best=1e9;d.slots.forEach((s,i)=>{const dx=(s.l+s.r)/2-x,dy=(s.t+s.b)/2-y,dd=dx*dx+dy*dy;if(dd<best){best=dd;at=i;}});}
  if(at!==d.at){d.at=at;place(at);}
  follow(e.clientX,e.clientY);
});
document.addEventListener('pointerup',()=>{
  const d=ldrag;if(!d.el)return;const el=d.el;d.el=null;if(!d.started)return;d.started=false;
  const swallow=ev=>{ev.stopImmediatePropagation();ev.preventDefault();};
  addEventListener('click',swallow,{capture:true,once:true});setTimeout(()=>removeEventListener('click',swallow,true),0);
  el.style.transition='transform .18s var(--ease)';el.style.transform='';
  setTimeout(()=>{el.classList.remove('held');el.removeAttribute('style');S.move(el.dataset.lkey,d.at);
    $('library').dataset.drawn='';render();},S.reduce()?0:190);
});
// ---- the fader plate ------------------------------------------------------------------------------
function buildPlate(){
  if($('channels').children.length)return;
  const marks=v=>`${v>0?'+'+v:v<0?'−'+(-v):'0'}`,pos=v=>(8-v)/16*100;
  const scale=[8,6,4,2,0,-2,-4,-6,-8].map(v=>`<span class="${v%4?'minor':''}" style="top:${pos(v)}%">${marks(v)}</span>`).join('');
  $('scaleL').innerHTML=`<div class="travel">${scale}</div>`;$('scaleR').innerHTML=`<div class="travel">${scale}</div>`;
  const rules=[8,4,0,-4,-8].map(v=>`<i class="${v===0?'zero':'major'}" style="top:${pos(v)}%"></i>`).join('');
  $('channels').innerHTML=`<div class="rules">${rules}</div>`+B.freqs.map((f,i)=>`<div class="chan" data-chan="${i}" style="--c:${region(i)};--i:${i}">
    <span class="val" id="v${i}" aria-hidden="true"></span>
    <div class="slot" data-band="${i}" tabindex="0" role="slider" aria-label="${S.hz(f)} hertz" aria-valuemin="-8" aria-valuemax="8"><div class="cap"></div></div>
    <span class="hz">${S.hz(f)}</span></div>`).join('');
}
function annunciators(p){
  const ann=(on,color,label)=>`<span class="ann ${on?'on '+color:''}"><span class="win"></span><span class="engr">${label}</span></span>`;
  $('ann').innerHTML=p.factory?ann(true,'white','Factory'):ui.draft?ann(true,'amber','Draft'):ann(true,'white','Saved');
}
function surface(){
  const p=editing();if(!p)return;
  buildPlate();
  const bands=editBands(),locked=!!p.factory;
  // Keep a name being typed, but always show the right profile's name after switching profiles.
  if(document.activeElement!==$('pname')||$('pname').dataset.key!==p.key){$('pname').value=p.name;$('pname').dataset.key=p.key;}
  $('pname').readOnly=locked;$('pname').tabIndex=locked?-1:0;$('channels').classList.toggle('locked',locked);$('display').classList.toggle('editable',!locked);
  annunciators(p);
  const v=locked?curveByBand(p):curveByBand(p,bands);
  if(p.key!==lastDisplay){lastDisplay=p.key;dplot.layout();dplot.pen(v);}else dplot.set(v);
  dplot.marks(locked?null:bands);dplot.hot(ui.hot);
  // The amber dashes: what the speaker plays, when that is not what the plate shows.
  const pl=S.linked()?playing():null,same=pl&&(pl.factory?locked&&pl.key===p.key:M.sameBands(pl.bands,bands));
  dplot.ghost(pl&&!same&&!draftPlaying()&&!aud.on?curveByBand(pl):null);
  bands.forEach((b,i)=>{const slot=$('channels').querySelector(`[data-band="${i}"]`);slot.classList.toggle('locked',locked);
    slot.setAttribute('aria-valuenow',b);slot.setAttribute('aria-valuetext',`${b>0?'plus ':b<0?'minus ':''}${Math.abs(b)} decibels${locked?', approximate':''}`);slot.setAttribute('aria-readonly',locked);
    slot.querySelector('.cap').style.top=`calc(${(8-b)/16} * var(--fh))`;$('v'+i).innerHTML=`<i>${locked?'≈':b>0?'+':b<0?'−':''}</i>${Math.abs(b)}`;});
  $('channels').querySelectorAll('.chan').forEach((c,i)=>c.classList.toggle('hot',i===ui.hot));
  const can=S.linked(),busy=S.busy==='apply',st=S.status(p);
  const applyLabel=p.factory||!ui.draft?'Play':'Play saved';
  const applyDis=!can||busy;
  // Already playing: the key becomes a lit indicator instead of a faded button.
  const applyKey=st==='playing'&&!draftPlaying()?`<button class="mech state" disabled aria-disabled="true" title="${ui.draft?'The saved version is playing':'Playing on the speaker'}">On speaker<span class="lamp"></span></button>`:null;
  const why=!can?'Connect the speaker first':ui.draft?'Play the saved version; Audition plays the draft':'Play this on the speaker';
  $('transport').innerHTML=locked?`<span class="engr">Transport</span>
      <button class="mech" data-do="dup" title="Make an editable copy">Duplicate</button>${applyKey||`<button class="mech go" data-do="apply" ${applyDis?'disabled':''} title="${why}">${applyLabel}</button>`}
      <span class="sep"></span><p class="note">Factory program: fixed in the speaker. The plate shows its shape as drawn by Tribit (≈ approximate).</p>`:
    `<span class="engr">Transport</span>
     <button class="mech" data-do="save" ${ui.draft?'':'disabled'} title="Save on this PC (Ctrl+S)">Save</button>
     <button class="mech" data-do="revert" ${ui.draft?'':'disabled'}>Revert</button>
     <button class="mech" data-do="audition" aria-pressed="${aud.on}" ${can?'':'disabled'} title="While lit, the speaker plays every change as you move the faders">Audition<span class="lamp"></span></button>
     ${applyKey||`<button class="mech go" data-do="apply" ${applyDis?'disabled':''} title="${why}">${applyLabel}</button>`}
     <span class="sep"></span>
     <button class="mech" data-do="dup">Duplicate</button>
     <button class="mech" data-do="flat">Flat</button>
     <button class="mech" data-do="del">Delete</button>`;
}
function render(){
  if(!S.ready())return;
  const off=!S.linked();
  if(off&&aud.on){aud.on=false;aud.before=null;}
  if(off)ui.choosing=null;
  $('app').classList.toggle('offline',off);
  instruments();speakerArt();bank();offlineCard();library();
  if(ui.mode==='listen')units();else surface();
  // An open list in the sheet would close if it were redrawn under the pointer.
  const f=document.activeElement;
  if(!$('sheet').hidden&&!(f?.tagName==='SELECT'&&$('sheet').contains(f)))sheet();
}
S.subscribe(render);
S.onAuditionError=err=>{aud.on=false;aud.before=null;toast('Audition stopped: '+err);render();};

// ---- device sheet ---------------------------------------------------------------------------------
const BEEPS=['Power','Pairing','Low battery','Max volume','Party (TWS)'];
// Settings, in three groups: the speaker itself, how it works with Windows, and the look.
// The widget's buttons are picked from a layout of the widget; the picker lists any number of
// programs in a scrolling list. The sheet is updated in place, so switches and the picker animate.
function sheet(){
  const s=S.speaker,st=S.settings,can=S.linked();
  const sw=(on,label,hint,act,dis)=>`<button class="dv-sw" data-dv="${act}" role="switch" aria-checked="${on}"${dis?' disabled':''}><span><b>${label}</b><small>${hint}</small></span><i></i></button>`;
  const keys=S.widget(),pick=ui.wpick,shown=pick??ui.lastPick??0;
  const item=p=>`<button class="pk" data-wset="${p.key}" aria-pressed="${keys[shown]===p.key}"><span>${esc(p.name)}</span></button>`;
  morph($('sheet'),`${s.error?`<p class="dv-err">${esc(s.error)}</p>`:''}
    <section><h4 class="engr">Speaker</h4>
      <div class="dv-label">Beeps</div>
      <div class="dv-chips">${BEEPS.map((l,i)=>`<button class="chip" data-dv="p${i}" aria-pressed="${s.prompts?.[i]===1}"${!can||s.prompts?.[i]<0?' disabled':''}>${l}</button>`).join('')}</div>
      ${sw(s.autoShutdown===1,'Auto shutdown','Turns off when nothing has played for a while','auto',!can||s.autoShutdown<0)}
    </section>
    <section><h4 class="engr">Windows</h4>
      <div class="dv-label">Widget buttons <small>Win + W</small></div>
      <div class="dv-wgrid">${keys.map((k,i)=>`<button class="wtile" data-wpick="${i}" aria-expanded="${pick===i}"><small>${i+1}</small>${esc(S.find(k).name)}</button>`).join('')}</div>
      <div class="dv-pick${pick!=null?' open':''}"><div><div class="dv-list" role="group" aria-label="Button ${shown+1} plays">
        <h5>Factory</h5>${S.factory.map(item).join('')}${S.custom().length?`<h5>Yours · ${S.custom().length}</h5>${S.custom().map(item).join('')}`:''}</div></div></div>
      ${sw(!!st.release,'Let the phone app connect','Frees the speaker shortly after the widget closes','release')}
      ${sw(!!st.logging,'Diagnostic log','Only needed for troubleshooting','logging')}
    </section>
    <section><h4 class="engr">Appearance</h4><div class="dv-seg" role="group" aria-label="Appearance">${[['light','Light'],['dark','Dark'],['device','Windows']].map(([v,l])=>
      `<button data-theme-choice="${v}" aria-pressed="${Theme.choice===v}">${l}</button>`).join('')}</div></section>
    <div class="dv-row"><button class="mech" data-dv="retry">Reconnect</button><button class="mech" data-dv="bt">Bluetooth</button></div>
    <div class="dv-remove">${ui.removing?`<p>Remove all profiles and settings from this PC?</p><div class="dv-row"><button class="mech danger" data-dv="remove-yes">Remove</button><button class="mech" data-dv="remove-no">Keep</button></div>`:
      `<button class="dv-link" data-dv="remove">Remove my data…</button>`}</div>
    <p class="dv-note">${s.firmware?`Firmware ${esc(s.firmware)}. `:''}Rename, power off, factory reset and firmware updates: use the Tribit app.</p>`);
}
// Brings `el` in line with `html` node by node, keeping the elements that are still there, so
// their CSS transitions run (switches slide, lamps fade) instead of being redrawn.
function morph(el,html){const t=document.createElement(el.tagName);t.innerHTML=html;sync(el,t);}
function sync(a,b){
  const an=[...a.childNodes],bn=[...b.childNodes];
  if(an.length!==bn.length||an.some((n,i)=>n.nodeName!==bn[i].nodeName)){a.replaceChildren(...bn);return;}
  an.forEach((n,i)=>{const m=bn[i];
    if(n.nodeType===3){if(n.nodeValue!==m.nodeValue)n.nodeValue=m.nodeValue;return;}
    if(n.nodeType!==1)return;
    for(const at of [...n.attributes])if(!m.hasAttribute(at.name))n.removeAttribute(at.name);
    for(const at of [...m.attributes])if(n.getAttribute(at.name)!==at.value)n.setAttribute(at.name,at.value);
    sync(n,m);});
}
document.addEventListener('click',e=>{
  // Redrawing the sheet can detach the clicked element, so stop here before the outside-click check.
  const wp=e.target.closest?.('[data-wpick]');if(wp){e.stopImmediatePropagation();const i=+wp.dataset.wpick;ui.wpick=ui.wpick===i?null:i;if(ui.wpick!=null)ui.lastPick=ui.wpick;sheet();return;}
  const ws=e.target.closest?.('[data-wset]');if(ws&&ui.wpick!=null){e.stopImmediatePropagation();S.setWidget(ui.wpick,ws.dataset.wset);ui.wpick=null;sheet();}
});
function toggleSheet(open){
  const el=$('sheet');$('deviceBtn').setAttribute('aria-expanded',open);clearTimeout(toggleSheet.t);el.classList.remove('closing');
  if(open){el.hidden=false;sheet();return;}
  if(el.hidden)return;el.classList.add('closing');toggleSheet.t=setTimeout(()=>{el.hidden=true;el.classList.remove('closing');},S.reduce()?0:160);
}

// ---- actions --------------------------------------------------------------------------------------
function toast(text,action){
  ui.toast?.remove();const t=document.createElement('div');t.className='toast';t.setAttribute('role','status');
  t.innerHTML=esc(text)+(action?`<button>${esc(action.label)}</button>`:'');
  if(action)t.querySelector('button').onclick=()=>{action.run();t.remove();};
  document.body.append(t);ui.toast=t;setTimeout(()=>{t.classList.add('out');setTimeout(()=>t.remove(),200);},action?7000:3200);
}
async function play(p){ui.pending=p.key;const r=await S.apply(p);ui.pending=null;if(!r.ok)toast(r.error);}
// Audition: remember what played before, stream the draft, and put it back when the draft goes.
const raw=bands=>({preset:M.CUSTOM,bands});
function auditionStart(){aud.on=true;const s=S.speaker;aud.before={preset:s.preset,bands:s.bands?.slice()};const b=editBands();if(b&&(s.preset!==M.CUSTOM||!M.sameBands(s.bands,b)))S.audition(raw(b));}
function auditionRestore(){if(!aud.before||!S.linked())return;const s=S.speaker,b=aud.before;
  if(s.preset===b.preset&&(b.preset!==M.CUSTOM||M.sameBands(s.bands,b.bands)))return;
  S.audition(b.preset===M.CUSTOM?raw(b.bands):{preset:b.preset});}
function auditionStop(restore){if(!aud.on)return;if(restore)auditionRestore();aud.on=false;aud.before=null;}
// Leaving a profile (another profile, Done, Delete) keeps its draft, which is saved with it;
// only what Audition played is undone.
function discardDraft(){
  if(aud.on){auditionRestore();aud.on=false;aud.before=null;}
  return true;
}
const openDraft=key=>{const p=S.find(key);ui.draft=p&&p.draft?p.draft.slice():null;};
const keepDraft=()=>{const p=editing();if(p&&!p.factory)S.setDraft(p.key,ui.draft);};
function enterEdit(key){
  const a=key?S.find(key):playing();
  const target=a&&!a.foreign?a:S.factory[1];
  S.transition(()=>{ui.mode='edit';ui.edit=target.key;openDraft(target.key);ui.choosing=null;lastDisplay=null;$('app').classList.add('editing');render();});
}
function leaveEdit(){if(!discardDraft())return;S.transition(()=>{ui.mode='listen';ui.hot=-1;$('app').classList.remove('editing');render();});}
function load(key){if(key===ui.edit)return;discardDraft();ui.edit=key;openDraft(key);render();}
function setBand(i,v){
  const p=editing();if(!p||p.factory)return;
  const b=(ui.draft||p.bands).slice();v=Math.max(-8,Math.min(8,Math.round(v)));if(b[i]===v)return;
  b[i]=v;ui.draft=M.sameBands(b,p.bands)?null:b;keepDraft();
  if(aud.on)S.audition(raw(b));   // queued: only the newest curve is sent
  surface();bank();
}
function newProfile(){
  if(ui.mode==='edit'&&!discardDraft())return;
  const from=ui.mode==='edit'?editing():playing();let key;
  if(from&&from.foreign){key=S.create({name:'From the speaker'});S.save(key,S.speaker.bands.slice());}
  else key=S.create({from:from?from.key:null});
  if(ui.mode==='edit'){ui.edit=key;ui.draft=null;render();}else enterEdit(key);
  setTimeout(()=>{$('pname').focus();$('pname').select();},ui.mode==='edit'?0:520);
}
async function assign(i,key){
  const p=S.find(key);if(!p||p.foreign)return;
  const r=await S.assign(i,key);toast(r.ok?`${Speaker.NAMES[i]} now selects ${p.name}`:r.error);
}
// Right-clicking a record opens a small menu: Edit, Duplicate, Delete (Delete offers Undo).
document.addEventListener('contextmenu',e=>{
  const r=e.target.closest('.rec[data-lkey]');if(!r){hideCtx();return;}e.preventDefault();
  const m=$('ctx');m.dataset.key=r.dataset.lkey;
  m.innerHTML='<button data-ctx="edit" role="menuitem">Edit</button><button data-ctx="dup" role="menuitem">Duplicate</button><span class="sep"></span><button data-ctx="del" class="danger" role="menuitem">Delete</button>';
  m.hidden=false;m.style.left=Math.min(e.clientX,innerWidth-m.offsetWidth-8)+'px';m.style.top=Math.min(e.clientY,innerHeight-m.offsetHeight-8)+'px';
  m.querySelector('button').focus({preventScroll:true});
});
function hideCtx(){$('ctx').hidden=true;}
$('libbody').addEventListener('scroll',hideCtx);
// Deleting keeps Undo; a profile open in the editor hands over to its neighbour.
function removeProfile(key){
  const p=S.find(key),i=S.custom().findIndex(x=>x.key===key),open=ui.mode==='edit'&&ui.edit===key;
  if(open)discardDraft();
  const undo=S.remove(key),left=S.custom();
  if(open){ui.edit=(left[i]||left[i-1]||S.factory[1]).key;openDraft(ui.edit);}
  render();toast(`Deleted ${p.name}`,{label:'Undo',run:()=>{S.undo(undo);if(open){ui.edit=undo.profile.key;openDraft(ui.edit);}render();}});
}
document.addEventListener('click',async e=>{
  const t=e.target;
  const cx=t.closest('[data-ctx]');
  if(cx){const key=$('ctx').dataset.key,what=cx.dataset.ctx;hideCtx();
    if(what==='edit'){toggleLibrary(false);ui.mode==='edit'?load(key):enterEdit(key);}
    else if(what==='dup'){const k=S.create({from:key});toast(`Made ${S.find(k).name}`);}
    else if(what==='del')removeProfile(key);
    return;}
  if(!t.closest('#ctx'))hideCtx();
  const win=t.closest('[data-win]');if(win){Bridge.send({type:'window',action:win.dataset.win});return;}
  const dv=t.closest('[data-dv]')?.dataset.dv;
  const th=t.closest('[data-theme-choice]');if(th){Theme.set(th.dataset.themeChoice);sheet();return;}
  if(dv){const s=S.speaker;
    if(dv==='close')toggleSheet(false);
    else if(dv[0]==='p'&&dv.length===2)S.setPrompt(+dv[1],s.prompts[+dv[1]]!==1).then(r=>!r.ok&&toast(r.error));
    else if(dv==='auto')S.setAutoShutdown(s.autoShutdown!==1).then(r=>!r.ok&&toast(r.error));
    else if(dv==='release')S.send({type:'setRelease',value:!S.settings.release});
    else if(dv==='logging')S.send({type:'setLogging',value:!S.settings.logging});
    else if(dv==='remove'||dv==='remove-no'){ui.removing=dv==='remove';sheet();}
    else if(dv==='remove-yes'){ui.removing=false;S.removeData();ui.mode==='edit'&&leaveEdit();toast('Your profiles and settings were removed');sheet();}
    else if(dv==='retry')S.send({type:'retry'});else if(dv==='bt')S.send({type:'openBluetooth'});
    return;}
  if(t.closest('#deviceBtn')){toggleSheet($('sheet').hidden);return;}
  if(!$('sheet').hidden&&!t.closest('#sheet')){ui.wpick=null;toggleSheet(false);}
  if(t.closest('#editKey')){ui.library=false;ui.mode==='edit'?leaveEdit():enterEdit();return;}
  const rw=t.closest('[data-rewrite]');if(rw){const i=+rw.dataset.rewrite;assign(i,S.slots()[i].key);return;}
  const arm=t.closest('[data-arm]');if(arm){const i=+arm.dataset.arm;ui.choosing=ui.choosing===i?null:i;render();return;}
  if(t.closest('[data-new]')){newProfile();return;}
  if(t.closest('[data-library]')){toggleLibrary(!ui.library);return;}
  const lc=t.closest('[data-lkey]');
  if(lc){const p=S.find(lc.dataset.lkey);
    if(ui.choosing!==null){const i=ui.choosing;ui.choosing=null;toggleLibrary(false);assign(i,p.key);return;}
    if(ui.mode==='edit'){toggleLibrary(false);load(p.key);return;}
    if(!S.linked()){toast('The speaker is not connected',{label:'Reconnect',run:()=>S.send({type:'retry'})});return;}
    play(p);return;}
  const k=t.closest('[data-key]');
  if(k){k.classList.add('down');setTimeout(()=>k.classList.remove('down'),120);
    const p=S.find(k.dataset.key);
    if(ui.choosing!==null){const i=ui.choosing;ui.choosing=null;render();assign(i,p.key);return;}
    if(ui.mode==='edit'){load(p.key);return;}
    if(!S.linked()){toast('The speaker is not connected',{label:'Reconnect',run:()=>S.send({type:'retry'})});return;}
    play(p);return;}
  const d=t.closest('[data-do]')?.dataset.do;if(!d)return;
  const p=editing();
  if(d==='save'){S.save(p.key,ui.draft);if(aud.on)aud.before={preset:M.CUSTOM,bands:ui.draft.slice()};ui.draft=null;toast(`Saved ${p.name} on this PC`);render();}
  else if(d==='revert'){
    // The draft goes back to the saved curve; Audition, if lit, follows it. Undo brings it back.
    const old=ui.draft;ui.draft=null;keepDraft();if(aud.on)S.audition(raw(p.bands));lastDisplay=null;render();
    toast('Draft discarded',{label:'Undo',run:()=>{if(ui.edit!==p.key)return;ui.draft=old;keepDraft();if(aud.on)S.audition(raw(old));lastDisplay=null;render();}});}
  else if(d==='flat'){const flat=B.freqs.map(()=>0);ui.draft=M.sameBands(p.bands,flat)?null:flat;keepDraft();if(aud.on)S.audition(raw(flat));lastDisplay=null;render();}
  else if(d==='dup'){discardDraft();const key=S.create({from:p.key});if(ui.draft)S.save(key,ui.draft.slice());ui.edit=key;ui.draft=null;render();$('pname').focus();$('pname').select();}
  else if(d==='audition'){if(aud.on)auditionStop(true);else auditionStart();render();}
  else if(d==='apply'){aud.on=false;aud.before=null;const r=await S.apply(p);toast(r.ok?`Playing ${p.name}`:r.error);render();}
  else if(d==='del')removeProfile(p.key);
});
$('pname').addEventListener('change',e=>{const p=editing();if(p&&!p.factory)S.rename(p.key,e.target.value);});
$('pname').addEventListener('keydown',e=>{if(e.key==='Enter'||e.key==='Escape')e.target.blur();});
document.addEventListener('keydown',e=>{
  if(e.key==='Escape'){if(!$('ctx').hidden){hideCtx();return;}if(!$('sheet').hidden){toggleSheet(false);return;}if(ui.library){toggleLibrary(false);return;}if(ui.choosing!==null){ui.choosing=null;render();return;}}
  const uk=e.altKey&&(e.key==='ArrowLeft'||e.key==='ArrowRight')&&e.target.closest?.('.pkey.user');
  if(uk){e.preventDefault();const key=uk.dataset.key,i=S.custom().findIndex(p=>p.key===key);S.move(key,i+(e.key==='ArrowRight'?1:-1));
    $('keys').querySelector(`[data-key="${key}"]`)?.focus();return;}
  if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='s'){e.preventDefault();if(ui.draft)document.querySelector('[data-do=save]')?.click();return;}
  const s=e.target.closest?.('.slot');if(!s)return;
  const i=+s.dataset.band,b=editBands()[i];
  const step={ArrowUp:1,ArrowDown:-1,PageUp:4,PageDown:-4}[e.key];
  if(step!=null){e.preventDefault();setBand(i,b+step);}
  else if(e.key==='Home'||e.key==='End'){e.preventDefault();setBand(i,e.key==='Home'?8:-8);}
  else if(e.key==='0'){setBand(i,0);}
  else if(e.key==='ArrowRight'||e.key==='ArrowLeft'){e.preventDefault();$('channels').querySelector(`[data-band="${Math.max(0,Math.min(N-1,i+(e.key==='ArrowRight'?1:-1)))}"]`)?.focus();}
});
// A band column lights up on the plate and on the display together.
function setHot(i){if(ui.hot===i)return;ui.hot=i;dplot.hot(i);$('channels').querySelectorAll('.chan').forEach((c,k)=>c.classList.toggle('hot',k===i));}
document.addEventListener('pointerover',e=>{if(ui.mode!=='edit')return;const c=e.target.closest('[data-chan]');if(c)setHot(+c.dataset.chan);});
document.addEventListener('focusin',e=>{const s=e.target.closest?.('.slot');if(s)setHot(+s.dataset.band);});
// The band column under the pointer on the display (its bezel sits 5px inside the channel width).
const colAt=(x,r)=>Math.max(0,Math.min(N-1,Math.floor((x-r.left+5)/(r.width+10)*N)));
$('display').addEventListener('pointermove',e=>{if(ui.mode==='edit'&&!e.buttons)setHot(colAt(e.clientX,$('dplot').getBoundingClientRect()));});
$('fplate').addEventListener('pointerleave',()=>setHot(-1));
// Faders follow the pointer; the display can be drawn on directly, column by column.
document.addEventListener('pointerdown',e=>{
  if(e.button!==0)return;
  const ch=ui.mode==='edit'&&e.target.closest('[data-chan]'),s=ch&&ch.querySelector('.slot');
  if(s&&!s.classList.contains('locked')){e.preventDefault();
    const i=+s.dataset.band,r=s.getBoundingClientRect();ch.setPointerCapture(e.pointerId);s.classList.add('drag');s.focus({preventScroll:true});
    const mv=ev=>setBand(i,8-(ev.clientY-r.top-13)/(r.height-26)*16);mv(e);
    ch.onpointermove=mv;ch.onpointerup=ch.onpointercancel=()=>{ch.onpointermove=null;s.classList.remove('drag');};return;}
  const disp=e.target.closest('#display');
  if(disp&&editing()&&!editing().factory){e.preventDefault();const r=$('dplot').getBoundingClientRect();disp.setPointerCapture(e.pointerId);
    const mv=ev=>{const i=colAt(ev.clientX,r);setHot(i);setBand(i,dplot.DB(ev.clientY-r.top));};
    mv(e);disp.onpointermove=mv;disp.onpointerup=disp.onpointercancel=()=>{disp.onpointermove=null;};}
});
// Double-clicking the cap itself zeroes the band; two quick presses on the track just move it.
document.addEventListener('dblclick',e=>{const c=ui.mode==='edit'&&e.target.closest('.cap');if(c)setBand(+c.closest('.slot').dataset.band,0);});
// Drag a program key onto a module (pressing the module's key, then a program, does the same).
// Dragging a program key moves the key itself (no browser drag image). Along the YOURS row it
// rearranges saved profiles: the key follows the pointer and the others slide aside. Lifted out of
// the row (or any factory key), it is carried over the light modules, and dropping it on one assigns it.
const drag={el:null,key:null,user:false,x0:0,y0:0,cx:0,cy:0,started:false,mode:null,row:null,scroll0:0,from:0,to:0,step:0,others:[],gap:null,rect:null};
function startDrag(e){
  const d=drag,el=d.el;d.started=true;try{el.setPointerCapture(e.pointerId);}catch{}
  d.rect=el.getBoundingClientRect();el.classList.add('held');
  if(d.user){d.row=el.parentElement;d.scroll0=d.row.scrollLeft;d.row.classList.add('sorting');
    const keys=[...d.row.querySelectorAll('.pkey.user:not([hidden])')];d.from=d.to=keys.indexOf(el);d.others=keys;
    d.step=keys.length>1?keys[1].getBoundingClientRect().left-keys[0].getBoundingClientRect().left:d.rect.width;
    d.mode='row';}else carry(e);
}
// Lift the key out of the row so it can travel over the modules; an empty slot holds its place.
function carry(e){
  const d=drag,el=d.el;if(d.mode==='carry')return;
  d.others.forEach(k=>{if(k!==el)k.style.transform='';});
  const r=el.getBoundingClientRect();d.gap=document.createElement('span');d.gap.className='pkey gap';el.before(d.gap);
  document.body.append(el);el.classList.add('carried');Object.assign(el.style,{left:r.left+'px',top:r.top+'px',transform:''});
  d.cx=e.clientX;d.cy=e.clientY;d.mode='carry';   // from here the key moves with the pointer from where it was lifted
}
function moveDrag(e){
  const d=drag,el=d.el;let dx=e.clientX-d.x0,dy=e.clientY-d.y0;
  if(d.mode==='row'&&dy<-48)carry(e);
  if(d.mode==='carry'){
    el.style.transform=`translate(${e.clientX-d.cx}px,${e.clientY-d.cy}px)`;
    const hit=document.elementFromPoint(e.clientX,e.clientY)?.closest('[data-unit]');
    document.querySelectorAll('.unit').forEach(u=>u.classList.toggle('drop',u===hit&&!u.querySelector('.lampkey').disabled));
    return;
  }
  const n=d.others.length;dx=Math.max(-d.from*d.step,Math.min((n-1-d.from)*d.step,dx));
  el.style.transform=`translateX(${dx}px) translateY(-4px)`;
  const to=Math.max(0,Math.min(n-1,d.from+Math.round(dx/d.step)));if(to===d.to)return;d.to=to;
  d.others.forEach((k,i)=>{if(k===el)return;const shift=i>d.from&&i<=to?-d.step:i<d.from&&i>=to?d.step:0;k.style.transform=shift?`translateX(${shift}px)`:'';});
}
function endDrag(commit){
  const d=drag,el=d.el;if(!el)return;
  document.querySelectorAll('.unit.drop').forEach(u=>u.classList.remove('drop'));
  const done=()=>{el.classList.remove('held','carried');el.removeAttribute('style');d.gap?.replaceWith(el);d.row?.classList.remove('sorting');
    d.others.forEach(k=>k.style.transform='');Object.assign(d,{el:null,started:false,mode:null,row:null,gap:null,others:[]});
    $('keys').dataset.html='';bank();};
  if(!commit){done();return;}
  if(d.mode==='carry'){const hit=commit.closest?.('[data-unit]');const key=d.key;done();if(hit&&!hit.querySelector('.lampkey').disabled)assign(+hit.dataset.unit,key);return;}
  // Settle the key into its new slot, then save the order (the redraw renumbers the keys).
  // The bank is the first six profiles, so a drop lands at the library position of the key it was dropped on.
  const key=d.key,to=d.to,from=d.from,target=S.custom().findIndex(p=>p.key===d.others[to]?.dataset.key);el.style.transition='transform .18s var(--ease)';el.style.transform=`translateX(${(to-from)*d.step}px)`;
  setTimeout(()=>{done();if(to!==from&&target>=0)S.move(key,target);},S.reduce()?0:180);
}
document.addEventListener('pointerdown',e=>{
  if(e.button!==0||drag.el)return;const k=e.target.closest('.pkey[data-key]');if(!k)return;
  Object.assign(drag,{el:k,key:k.dataset.key,user:k.classList.contains('user')&&!k.classList.contains('visiting'),x0:e.clientX,y0:e.clientY,started:false,mode:null,gap:null,others:[]});
});
document.addEventListener('pointermove',e=>{
  if(!drag.el)return;
  if(!drag.started){if(Math.hypot(e.clientX-drag.x0,e.clientY-drag.y0)<6)return;startDrag(e);}
  e.preventDefault();moveDrag(e);
});
document.addEventListener('pointerup',e=>{
  if(!drag.el)return;if(!drag.started){drag.el=null;return;}
  // The click that follows a drag must not also play the program.
  const swallow=ev=>{ev.stopImmediatePropagation();ev.preventDefault();};
  addEventListener('click',swallow,{capture:true,once:true});setTimeout(()=>removeEventListener('click',swallow,true),0);
  endDrag(drag.mode==='carry'?(document.elementFromPoint(e.clientX,e.clientY)||document.body):true);
});
document.addEventListener('pointercancel',()=>{if(drag.started)endDrag(false);else drag.el=null;});
document.addEventListener('keydown',e=>{if(e.key==='Escape'&&drag.started)endDrag(false);});
addEventListener('resize',()=>{if(ui.mode==='listen')conduits();const row=document.querySelector('.userrow');if(row)fit(row);});
// Re-measure once the speaker and modules have settled after a mode change.
$('rig').addEventListener('animationend',e=>{if(e.target.classList.contains('spkwrap'))conduits();});
render();
// #edit opens the editor on the first saved profile, #device the sheet (reviews and screenshots).
if(location.hash==='#device')toggleSheet(true);
if(location.hash==='#edit'){const go=()=>{const k=S.custom()[0]?.key;if(k){ui.mode='edit';ui.edit=k;$('app').classList.add('editing');render();}};
  if(S.ready())go();else{const off=S.subscribe(()=>{if(S.ready()){off();go();}});}}
})();
