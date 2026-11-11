// Sound Lab. Two modes on one instrument:
//   listening – assignment modules for the three EQ-button lights, the speaker, the program bank
//   editing   – the bank's display unfolds into a fader plate whose columns line up with the curve
// Saving (this PC), applying (plays the saved version) and assigning (writes the button's
// lights) stay separate. The speaker only ever plays an unsaved draft while AUDITION is lit.
(function(){
'use strict';
const S=Store,M=S.M,B=M.BANDS,N=B.freqs.length,$=id=>document.getElementById(id),esc=S.esc;
const region=i=>i<3?'var(--band-low)':i<6?'var(--band-mid)':'var(--band-high)';
const ui={mode:'listen',edit:null,opened:null,hold:false,choosing:null,hot:-1,toast:null,pending:new Set(),wpick:null,lastPick:null,library:false,removing:false,armed:null};

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
  if(was!==(viz.playing&&viz.listening)&&S.ready()){bank();library();}   // the record stops or turns at once
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
const editBands=()=>{const p=editing();return p?(p.factory?S.bandsOf(p):p.bands):null;};
const pad2=n=>n==null?'··':String(n).padStart(2,'0');
const lightsOf=key=>S.slots().map((v,i)=>v.key===key?i:-1).filter(i=>i>=0);
const nameOf=p=>!p?'—':p.foreign?'Custom curve':p.name;

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
  if(!$('spk').firstChild)$('spk').innerHTML=Speaker.svg({light:lit,state,id:'big'});
  Speaker.set($('spk'),{light:lit,state});
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
    setTimeout(()=>{el.classList.remove('closing');if(S.linked())el.hidden=true;},S.reduce()?0:S.motion('t-norm'));}return;}
  el.classList.remove('closing');el.hidden=false;
  // Shown only for the moment before the Speakers page takes over (the grace period after a drop).
  const seen=lastSeen(s.lastSeen);
  const r=M.dropReason(s.reason,s.error);
  morph(el,`<span class="engr"><span class="led ${connecting?'amber blink':'orange'}"></span>${connecting?'Connecting':esc(r.word)}</span>
    <p>${connecting?'Opening the speaker’s controls…':esc(r.hint)}${seen&&!connecting?`<br>Last seen ${seen}; the lights and programs show what was read then.`:''}</p>
    ${connecting?'':reasonKeys(r)}`);
}
// The keys a missing link can offer: Try again only for busy controls; Bluetooth settings otherwise.
function reasonKeys(r,go=false){
  const k=r.key==='retry'?`<button class="mech${go?' go':''}" data-dv="retry">Try again</button>`:r.key==='bt'?`<button class="mech${go?' go':''}" data-dv="bt">Bluetooth settings</button>`:'';
  return k?`<div class="acts">${k}</div>`:'';
}
// The name on the title bar: the speaker in use (its Windows name when two of one model are
// paired), or the app's own name while nothing is in use (booting, the Speakers page).
function titleName(){return $('app').classList.contains('booting')||rk.why?'Tribit Connect':speakerName();}
function speakerName(){
  const sp=S.speakers,d=sp?.list.find(x=>x.address===sp.address);if(!d)return 'XSound Plus 2';
  return sp.list.some(o=>o!==d&&o.model===d.model)?d.name:d.model;
}
// The title bar's key: ‹ Speakers on a speaker's page. On a Speakers page opened by hand, with the
// speaker still linked, it is the way back: ‹ and that speaker's name (Esc does the same).
function backKey(){
  const back=rk.why==='manual'&&S.linked(),text=rk.why?`‹ ${speakerName()}`:'‹ Speakers',b=$('backBtn');
  $('app').classList.toggle('returnable',back);
  if(b.textContent!==text)b.textContent=text;   // no title: the app shows no tooltips
}
const lastSeen=ms=>M.whenSeen(ms);
// ---- Speakers page ---------------------------------------------------------------------------------
// The paired Tribit speakers, on a SPEAKERS selector with the one selected on the stage. It appears only when the app cannot carry on by itself
// (M.rackReason: the speaker gone past the grace period, or two to choose from), or when the model
// name is clicked. The editor stays as it was underneath, so a speaker coming back returns to it,
// draft and all; Audition stops with the link (render), and the sheet and library close.
const rk={why:null,downAt:0,timer:0,poll:0,launch:true,switchAt:0,holdUntil:0},SWITCH_WAIT=6000,SHOW_HOLD=1200;
function rackTick(){
  if(!S.ready())return;
  const s=S.speaker,linked=S.linked(),hidden=document.hidden,booting=!S.settled;
  // Until the first connect attempt has finished, only the title bar shows (no instrument flash).
  // The cover lifts only after the launch decision, so the instrument never shows before the page.
  const wasBooting=$('app').classList.contains('booting');
  if(linked||hidden)rk.downAt=0;else if(!rk.downAt)rk.downAt=Date.now();
  const downFor=rk.downAt?Date.now()-rk.downAt:0;
  // Shown again from the tray: the host starts connecting a moment after the window appears, so the
  // decision waits briefly (under the cover) rather than taking the released link for a missing speaker.
  const holding=Date.now()<rk.holdUntil;
  const why=M.rackReason({linked,status:s.status,hidden,downFor,list:S.speakers?.list,chosen:S.chosen,booting:booting||holding,launch:rk.launch});
  // The launch decision is made once, as soon as the attempt settles (connecting waits for its end).
  if(!booting&&!holding&&s.status!=='connecting')rk.launch=false;
  clearTimeout(rk.timer);
  if(holding)rk.timer=setTimeout(rackTick,Math.max(30,rk.holdUntil-Date.now()+20));
  else if(!why&&rk.downAt)rk.timer=setTimeout(rackTick,Math.max(50,M.RACK_GRACE-downFor+20));
  // After a switch the page waits for the old link to drop and the new one to come up.
  // A switch the host did not take up (the speaker was unpaired meanwhile, say) must not hold the page
  // for good: after SWITCH_WAIT with the old link still up, it is an ordinary manually opened page.
  if(rk.why==='switch'){if(!linked)rk.dropped=true;else if(rk.dropped){closeRack();return;}
    else if(Date.now()-rk.switchAt>SWITCH_WAIT)rk.why='manual';
    else{clearTimeout(rk.timer);rk.timer=setTimeout(rackTick,SWITCH_WAIT+50);}
    if(rk.why==='switch')return;}
  if(why&&!rk.why)openRack(why,wasBooting);
  // A page opened for a missing speaker stays until it links (not merely until the rule goes quiet).
  else if(!why&&rk.why&&rk.why!=='manual'&&(rk.why!=='down'||linked))closeRack();
  if(wasBooting&&!booting&&!holding&&(s.status!=='connecting'||S.bootOver)){$('app').classList.remove('booting');
    // The cover lifts into the page, it does not cut to it.
    if(!S.reduce()){$('app').classList.add('arriving');setTimeout(()=>$('app').classList.remove('arriving'),S.enterMs());}
    render();}
}
// instant: the launch decision, made under the boot cover, so there is nothing to fade from.
function openRack(why,instant=false){
  if(rk.why)return;rk.why=why;rk.order=null;rk.sel=null;
  // Nothing of the page being left stays live: a key in mid-drag goes home, and a held compare ends.
  if(drag.el)endDrag(false);cancelLibraryDrag();endHold();
  hideCtx();toggleSheet(false);ui.choosing=null;if(ui.library)toggleLibrary(false);
  S.listSpeakers();clearInterval(rk.poll);rk.poll=setInterval(()=>{if(!document.hidden)S.listSpeakers();},4000);   // a speaker paired meanwhile appears
  // The editor is left as DONE leaves it, so opening a speaker always lands on its home page.
  const was=ui.mode==='edit'?ui.edit:null;
  const show=()=>{if(was){ui.mode='listen';ui.hot=-1;$('app').classList.remove('editing');syncLights(was);}
    $('rack').hidden=false;$('app').classList.add('racked');$('modelBtn').textContent=titleName();backKey();rack();};
  if(instant)show();else{swapBank();S.transition(show);}
}
function closeRack(){
  if(!rk.why)return;rk.why=null;clearInterval(rk.poll);toggleSheet(false);   // the app's settings belong to the page being left
  swapBank();S.transition(()=>{$('rack').hidden=true;$('app').classList.remove('racked');lastDisplay=null;render();});
}
// Between the Speakers page and a speaker's home page the bottom bar changes keys: the old ones drop
// out with the page, the new ones rise in (styles under .swapping).
function swapBank(){if(S.reduce())return;const a=$('app');a.classList.add('swapping');clearTimeout(swapBank.t);swapBank.t=setTimeout(()=>a.classList.remove('swapping'),S.exitMs()+S.enterMs());}
function rack(){
  const sp=S.speakers,s=S.speaker,here=sp?.address;
  // The order is set when the page opens and kept while it is up, so a switch never reshuffles it.
  if(!rk.order&&sp)rk.order=M.rackOrder(sp.list,here).map(d=>d.address);
  const at=d=>{const i=rk.order?rk.order.indexOf(d.address):-1;return i<0?1e3:i;};
  const list=M.rackOrder(sp?.list,here).sort((a,b)=>at(a)-at(b));
  // Two of the same model go by the names they have in Windows.
  const twin=d=>list.some(o=>o!==d&&o.model===d.model);
  const label=d=>twin(d)?d.name:d.model;
  // The key selected: the one in use, until another is pressed.
  if(!list.some(d=>d.address===rk.sel))rk.sel=(list.find(d=>d.address===here)||list.find(d=>d.supported)||list[0])?.address;
  const view=d=>{const mine=d.address===here,linked=mine&&S.linked(),connecting=mine&&s.status==='connecting';
    return {mine,linked,connecting,dropped:mine&&!linked&&!connecting};};
  // ---- the selector: A, B, C… as on an amp; the lamp is the link (this app's, or Windows' for the others)
  const key=(d,i)=>{const v=view(d),lamp=v.connecting?'busy':v.linked||(!v.mine&&d.connected)?'on':v.dropped?'err':'';
    return `<button class="pkey spk${d.supported?'':' foreign'}" role="radio" aria-checked="${d.address===rk.sel}" data-tune="${esc(d.address)}" tabindex="${d.address===rk.sel?0:-1}">
      <span class="led ${lamp}"></span><span class="n">${String.fromCharCode(65+i)}</span><span class="t">${esc(label(d))}</span></button>`;};
  const sup=list.filter(d=>d.supported),other=list.filter(d=>!d.supported);let i=0;
  const group=(name,ds)=>ds.length?`<div class="group"><span class="group-label">${name}</span><div class="row">${ds.map(d=>key(d,i++)).join('')}</div></div>`:'';
  // Pairing happens in Windows; the dashed key, like "+ new" on the program bank, opens it.
  const pair='<div class="group"><span class="group-label blank">&nbsp;</span><div class="row"><button class="pkey add spk" data-dv="bt" aria-label="Pair a speaker in Bluetooth settings"><span class="n">+</span><span class="t">pair</span></button></div></div>';
  morph($('spkkeys'),!sp?'':group('SUPPORTED',sup)+group('NOT SUPPORTED YET',other)+pair);
  // ---- the stage: the speaker selected
  const d=list.find(x=>x.address===rk.sel),art=$('pickart');
  if(!d){art.hidden=true;
    morph($('pickinfo'),!sp?'':`<h2>No Tribit speaker</h2><p>Pair your speaker with this PC in Bluetooth settings, and it appears here.</p>
      <div class="acts"><button class="mech go" data-dv="bt">Bluetooth settings</button></div>`);return;}
  const v=view(d);
  // Only the XSound Plus 2 has a drawing; other models get their name, never a guessed shape.
  art.hidden=!d.supported;
  if(d.supported){
    const state=v.connecting?'connecting':v.linked||(!v.mine&&d.connected)?'linked':'off';
    if(!art.firstChild)art.innerHTML=Speaker.svg({light:-1,state,id:'pick',interactive:false});
    Speaker.set(art,{light:-1,state});art.classList.toggle('off',state==='off');   // a picture: the EQ ring stays unlit
  }
  // The speaker in use without a link says why (M.dropReason) and offers only a key that can help:
  // Try again for busy controls, Bluetooth settings otherwise. Never "Reconnect".
  const r=v.dropped?M.dropReason(s.reason,s.error):null;
  const seen=!v.linked&&!v.connecting&&d.seen?`Last seen ${lastSeen(d.seen)}`:'';
  const batt=v.linked&&s.battery>=0?` · ${Math.max(0,Math.min(100,s.battery))}%`:'';
  const [led,word]=!d.supported?['','']:v.connecting?['amber blink','Connecting']:v.linked?['on','Connected'+batt]:r?['orange',r.word]:
    d.connected?['','']:['','Off or out of range'];
  const act=!d.supported?`<button class="mech go" data-help="${esc(d.model)}">Help add it</button>`:
    v.connecting?'':r?reasonKeys(r,true).replace(/^<div class="acts">|<\/div>$/g,''):
    `<button class="mech go" data-open="${esc(d.address)}">Open</button>`;
  const why=!d.supported?`Tribit Connect cannot control the ${esc(d.model)} yet. A short Bluetooth log from your phone helps add it.`:
    r?esc(r.hint):!v.mine&&!d.connected?'Turn it on and choose Open; the app connects once Windows has it.':'';
  // The name is on the stage only when there is no drawing, or when two of one model need telling apart.
  morph($('pickinfo'),`${!d.supported||twin(d)?`<h2>${esc(label(d))}</h2>`:''}${word?`<span class="state engr">${led?`<span class="led ${led}"></span>`:''}${word}${seen?` · ${seen}`:''}</span>`:''}
    ${why?`<p>${why}</p>`:''}<div class="acts">${act}</div>`);
}
// Selecting: press a key, or the arrow keys along the selector.
function tune(address){rk.sel=address;rack();$('spkkeys').querySelector(`[data-tune="${CSS.escape(address)}"]`)?.focus({preventScroll:true});}
$('spkkeys').addEventListener('click',e=>{const k=e.target.closest('[data-tune]');if(k)tune(k.dataset.tune);});
// Enter or a double-click goes straight in, when the stage offers Open for that speaker.
const openSelected=()=>$('pickinfo').querySelector('[data-open]')?.click();
$('spkkeys').addEventListener('dblclick',e=>{const k=e.target.closest('[data-tune]');if(k){tune(k.dataset.tune);openSelected();}});
$('spkkeys').addEventListener('keydown',e=>{
  const k=e.key==='Enter'&&e.target.closest('[data-tune]');if(k){e.preventDefault();tune(k.dataset.tune);openSelected();return;}
  const step={ArrowLeft:-1,ArrowUp:-1,ArrowRight:1,ArrowDown:1}[e.key];if(!step)return;e.preventDefault();
  const all=[...$('spkkeys').querySelectorAll('[data-tune]')],i=all.findIndex(b=>b.dataset.tune===rk.sel);
  const next=all[Math.max(0,Math.min(all.length-1,i+step))];if(next)tune(next.dataset.tune);
});
// Shown again (from the tray, or for the first time after starting with Windows): decide as at launch,
// with no grace, so the instrument is not shown for three seconds before this page takes over.
// The cover goes back up for that moment, as at launch.
document.addEventListener('visibilitychange',()=>{
  if(!document.hidden&&!rk.why&&S.ready()&&S.settled&&!S.linked()){rk.launch=true;rk.holdUntil=Date.now()+SHOW_HOLD;$('app').classList.add('booting');}
  rackTick();});
// The window opens cloaked (src/webview_host.hpp) and the host waits for this page to say it has drawn, so it
// never shows a blank frame. Back from the tray, the page comes in as a page change does; the first opening
// has its own entrance (the cover lifting, above).
let revealed=false;
Bridge.on(m=>{
  if(m.type!=='reveal')return;
  const app=$('app');
  if(revealed&&!S.reduce()&&!app.classList.contains('booting')){
    app.classList.remove('leaving');app.classList.add('arriving');
    clearTimeout(revealed.t);revealed={t:setTimeout(()=>app.classList.remove('arriving'),S.enterMs())};}
  else revealed=revealed||{};
  // Frames may be paused while the window is cloaked (it counts as hidden), so a timer answers too.
  let sent=false;const done=()=>{if(!sent){sent=true;Bridge.send({type:'painted'});}};
  requestAnimationFrame(()=>requestAnimationFrame(done));setTimeout(done,60);});

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
  const key=(p,cls)=>{const lights=lightsOf(p.key);return `<button class="pkey prog ${p.factory?'':'user '}${cls}" data-key="${p.key}">
    <span class="led"></span><span class="n">${pad2(S.number(p.key))}</span>${lights.length?`<span class="dot">${lights.map(i=>`<i class="l${i}"></i>`).join('')}</span>`:''}<span class="t" data-name="${esc(p.name)}">${esc(p.name)}</span></button>`;};
  // The bar is the first twelve programs, the user's own and the factory ones alike, in the user's
  // order. One being edited that is not among them visits as an extra key, so the editor always has
  // a lit key. "+ new" is an editor key: it folds away on the home page (CSS), so the row never
  // rebuilds when the mode changes.
  const all=S.programs(),onBar=all.slice(0,M.BANK),visit=editKey&&!onBar.some(p=>p.key===editKey)?S.find(editKey):null;
  const html=`<div class="group user"><div class="row userrow">${onBar.map(p=>key(p,'')).join('')}${visit?key(visit,'visiting'):''}
    <button class="pkey add" data-new aria-label="New program"><span class="n">+</span><span class="t">new</span></button>
    <button class="pkey lib" data-library aria-controls="library"><svg width="22" height="16" viewBox="0 0 22 16" aria-hidden="true">${[0,8,16].map(x=>[0,9].map(y=>`<rect x="${x}" y="${y}" width="6" height="7" rx="1.2"/>`).join('')).join('')}</svg><span class="t">All ${all.length}</span></button></div></div>`;
  if($('keys').dataset.html!==html&&!drag.started){$('keys').dataset.html=html;$('keys').innerHTML=html;}
  $('keys').querySelector('[data-new]').inert=ui.mode!=='edit';
  const lib=$('keys').querySelector('[data-library]');lib.setAttribute('aria-expanded',ui.library);lib.setAttribute('aria-label',`All ${all.length} programs`);
  $('keys').querySelectorAll('[data-key]').forEach(k=>{const p=S.find(k.dataset.key);if(!p)return;const on=active===p.key,ed=editKey===p.key;
    k.classList.toggle('playing',on);k.classList.toggle('busy',ui.pending.has(p.key));k.classList.toggle('editing',ed);
    k.setAttribute('aria-label',`${pad2(S.number(p.key))} ${p.name}${on?', playing':''}`);
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
// narrowest keys do not fit, whole keys drop from the end; All has every program.
function fit(row){
  const keys=$('keys'),wrap=document.querySelector('.lcdwrap'),lcd=$('lcd'),prog=[...row.querySelectorAll('.pkey.prog')];
  const lcdW=ui.mode!=='edit'&&getComputedStyle(lcd).display!=='none'?Math.min(210,Math.max(150,innerWidth*.13))+14:0;
  const room=keys.clientWidth+wrap.offsetWidth-(lcdW+$('editKey').offsetWidth);
  const gap=parseFloat(getComputedStyle(row).columnGap)||8,more=ui.mode==='edit'?1:0,lib=46;
  const width=n=>(room-lib-10-gap*(n+more))/(n+more);
  let n=prog.length;while(n>1&&width(n)<56)n--;
  prog.forEach((k,i)=>k.hidden=i>=n&&!k.classList.contains('visiting'));
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
// ---- library: every program, on a page of its own -------------------------------------------------
// Each program is a record: a square sleeve, like an album cover (number, curve, name), with its disc
// peeking out. They are in the user's order, six to a row: the first row is what the widget shows and
// the first two are the program bar. Dragging rearranges them; right-click has the rest.
const MINI_GRID=`M0 7.5H100M0 22.5H100${[1,2,3,4,5,6,7,8].map(i=>`M${(i*100/9).toFixed(1)} 0V30`).join('')}`;
const mini=p=>{const v=curveByBand(p,null,48),line=v.map((d,k)=>(k?'L':'M')+(k/(v.length-1)*100).toFixed(1)+' '+(15-d*1.6).toFixed(1)).join('');
  return `<svg viewBox="0 0 100 30" preserveAspectRatio="none" aria-hidden="true"><path class="g" d="${MINI_GRID}"/><path class="z" d="M0 15H100"/>
    <path class="f" d="${line}L100 30L0 30Z"/><path class="glow" d="${line}"/><path class="c" d="${line}"/></svg>`;};
const record=p=>{const lights=lightsOf(p.key);return `<div class="rec${p.factory?' factory':''}" data-lkey="${p.key}" role="button" tabindex="0">
  <span class="discwrap" aria-hidden="true"><svg class="disc" viewBox="0 0 100 100"><use href="#disc"/></svg></span>
  <span class="sleeve"><span class="n">${pad2(S.number(p.key))}</span>${p.factory?'<span class="tag">≈ Factory</span>':''}
    <span class="mini">${mini(p)}</span><span class="t" data-name="${esc(p.name)}">${esc(p.name)}</span>
    ${lights.length?`<span class="dot">${lights.map(i=>`<i class="l${i}"></i>`).join('')}</span>`:''}</span></div>`;};
// The playing record turns while music plays and stops where it is when it pauses. Every disc shares one
// position on the page's animation timeline: turning, all of them start from spin.start; stopped, all hold
// spin.at. So a disc drawn anew (a reorder rebuilds the grid) is at exactly the old one's angle in the same
// frame, and a pause or resume carries on from where the disc is rather than restarting it.
const SPIN=[{transform:'rotate(0deg)'},{transform:'rotate(360deg)'}],spin={start:0,at:0};   // at: ms into the turn while stopped, else null
function spinMusic(turning){const now=document.timeline.currentTime??0;
  if(turning&&spin.at!==null){spin.start=now-spin.at;spin.at=null;}
  else if(!turning&&spin.at===null)spin.at=now-spin.start;}
function spinDisc(disc,on){
  let a=disc.getAnimations().find(x=>x.id==='spin');
  if(!on||S.reduce()){a?.cancel();return;}
  if(!a){a=disc.animate(SPIN,{duration:S.motion('t-spin')||8000,iterations:Infinity,easing:'linear'});a.id='spin';a.pause();a.currentTime=0;}
  if(spin.at===null){if(a.playState!=='running'||a.startTime!==spin.start)a.startTime=spin.start;}
  else if(a.playState!=='paused'||a.currentTime!==spin.at){a.pause();a.currentTime=spin.at;}
}
function library(){
  const el=$('library');el.classList.toggle('open',ui.library);el.inert=!ui.library;$('app').classList.toggle('lib-open',ui.library);
  if(!ui.library&&!el.dataset.drawn)return;
  const q=$('libsearch').value.trim().toLowerCase(),all=S.programs(),active=S.linked()?S.active():null;
  const html=all.map(record).join('');
  // A rebuilt grid (after a drop, say) is drawn where it ends up: for its first frames nothing transitions, so a
  // disc under the pointer appears raised instead of dropping in and lifting out again.
  if(el.dataset.drawn!==html&&!ldrag.started){el.dataset.drawn=html;const g=$('libgrid');g.innerHTML=html;
    g.classList.add('fresh');requestAnimationFrame(()=>requestAnimationFrame(()=>g.classList.remove('fresh')));}
  spinMusic(!!active&&viz.listening&&viz.playing);
  let shown=0;const roll=[];
  // Every record's state is set before anything is measured (rollName reads widths): a record drawn anew is
  // first styled already lifted as playing, so its disc does not dip and rise again after a reorder.
  el.querySelectorAll('[data-lkey]').forEach(c=>{const p=S.find(c.dataset.lkey);if(!p)return;
    c.hidden=!!q&&!(p.name.toLowerCase().includes(q)||pad2(S.number(p.key)).includes(q));shown+=!c.hidden;
    const on=active===p.key,ed=ui.mode==='edit'&&ui.edit===p.key;
    c.classList.toggle('playing',on);c.classList.toggle('editing',ed);c.classList.toggle('busy',ui.pending.has(p.key));
    spinDisc(c.querySelector('.disc'),on);roll.push([c.querySelector('.t'),on||ed]);});
  roll.forEach(([t,on])=>rollName(t,on));
  $('libcount').textContent=q?`${shown} of ${all.length}`:all.length;$('libempty').hidden=!q||shown>0;
}
function toggleLibrary(open){if(ui.library===open)return;ui.library=open;$('libsearch').value='';hideCtx();render();}
$('libsearch').addEventListener('input',()=>library());
// The bank's display folds away in the editor; once it has, the row knows its real width.
$('lcd').addEventListener('transitionend',e=>{if(e.propertyName==='width')fit($('keys').querySelector('.userrow'));});
// Records are arranged by dragging (not while searching). Slots are measured once when the drag
// starts, and the target is the slot under the pointer: testing against cards that are still
// gliding made them swap back and forth.
const ldrag={el:null,started:false,x0:0,y0:0,gx:0,gy:0,slots:[],at:-1,scroll0:0};
function flip(box,change,skip){
  const items=[...box.children].filter(c=>c!==skip),before=new Map(items.map(c=>[c,c.getBoundingClientRect()]));
  change();if(S.reduce())return;
  items.forEach(c=>{const a=before.get(c),b=c.getBoundingClientRect(),dx=a.left-b.left,dy=a.top-b.top;if(!dx&&!dy)return;
    c.style.transition='none';c.style.transform=`translate(${dx}px,${dy}px)`;c.getBoundingClientRect();
    c.style.transition='transform .24s var(--ease)';c.style.transform='';});
}
function follow(x,y){const d=ldrag,el=d.el;el.style.transform='';const r=el.getBoundingClientRect();el.style.transform=`translate(${x-d.gx-r.left}px,${y-d.gy-r.top}px)`;}
// Puts the dragged record at list position `at`.
function place(at){
  const grid=$('libgrid'),el=ldrag.el,list=[...grid.children].filter(c=>c!==el);
  list.splice(at,0,el);flip(grid,()=>{list.forEach(c=>grid.appendChild(c));},el);
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
    const recs=[...$('libgrid').children];d.at=recs.indexOf(d.el);
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
// A drag that is cancelled (the pointer is taken away, or the page changes) puts the record back.
function cancelLibraryDrag(){
  const d=ldrag,el=d.el;if(!el)return;d.el=null;
  if(d.started){d.started=false;el.classList.remove('held');el.removeAttribute('style');$('library').dataset.drawn='';library();}
}
document.addEventListener('pointercancel',cancelLibraryDrag);
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
  const on=S.linked()&&S.active()===p.key;
  morph($('ann'),(p.factory?ann(true,'white','Factory'):'')+ann(ui.hold||on,'amber',ui.hold?'Playing how it was':'Playing on the speaker'));
}
// The editor is live: the plate shows the program as it is, the speaker plays it, and every change is
// kept as it is made. There is nothing to save. COMPARE latches, with its lamp lit: the speaker plays the
// program as it was when it came onto the plate, and the curve as it is now stays on the display,
// dashed; press it again (or move a fader) to return. UNDO goes back to how it was. DUPLICATE, FLAT and
// DELETE rest at the foot of the transport, as the plate's other keys always have.
function surface(){
  const p=editing();if(!p)return;
  buildPlate();
  const bands=editBands(),locked=!S.linked(),approx=!!p.factory;
  // Keep a name being typed, but always show the right profile's name after switching profiles.
  if(document.activeElement!==$('pname')||$('pname').dataset.key!==p.key){$('pname').value=p.name;$('pname').dataset.key=p.key;}
  $('pname').readOnly=approx;$('pname').tabIndex=approx?-1:0;$('channels').classList.toggle('locked',locked);$('display').classList.toggle('editable',!locked);
  annunciators(p);
  const was=ui.hold&&ui.opened?.key===p.key?ui.opened.bands:null;
  const v=was?curveByBand(p,was):approx?curveByBand(p):curveByBand(p,bands);
  if(p.key!==lastDisplay){lastDisplay=p.key;dplot.layout();dplot.pen(v);}else dplot.set(v);
  dplot.marks(approx||was?null:bands);dplot.hot(ui.hot);
  dplot.ghost(was?curveByBand(p,bands):null);
  bands.forEach((b,i)=>{const slot=$('channels').querySelector(`[data-band="${i}"]`);slot.classList.toggle('locked',locked);
    slot.setAttribute('aria-valuenow',b);slot.setAttribute('aria-valuetext',`${b>0?'plus ':b<0?'minus ':''}${Math.abs(b)} decibels${approx?', approximate':''}`);slot.setAttribute('aria-readonly',locked);
    slot.querySelector('.cap').style.top=`calc(${(8-b)/16} * var(--fh))`;$('v'+i).innerHTML=`<i>${approx?'≈':b>0?'+':b<0?'−':''}</i>${Math.abs(b)}`;});
  $('channels').querySelectorAll('.chan').forEach((c,i)=>c.classList.toggle('hot',i===ui.hot));
  // The keys are part of the plate and never grey out. UNDO sits pressed in while there is nothing to
  // undo, pops up as soon as a change is made, and goes back in when pressed. COMPARE latches with its lamp.
  const changed=!!ui.opened&&ui.opened.key===p.key&&!M.sameBands(ui.opened.bands,bands);
  morph($('transport'),`<span class="engr">Sound</span>
     <button class="mech" data-do="compare" aria-pressed="${ui.hold}">Compare<span class="lamp"></span></button>
     <button class="mech undo${changed?'':' sunk'}" data-do="undo" aria-disabled="${!changed}" aria-keyshortcuts="Control+Z">Undo</button>
     <span class="sep"></span>
     <button class="mech" data-do="dup">Duplicate</button>
     ${approx?'':`<button class="mech" data-do="flat">Flat</button>
     <button class="mech del${ui.armed===p.key?' armed':''}" data-do="del" aria-pressed="${ui.armed===p.key}">${ui.armed===p.key?'Confirm':'Delete'}<span class="lamp"></span></button>`}`);
}
function render(){
  if(!S.ready())return;
  const off=!S.linked();
  if(off)ui.hold=false;
  if(off)ui.choosing=null;
  $('app').classList.toggle('offline',off);
  instruments();speakerArt();bank();offlineCard();library();
  if(!$('rack').hidden)rack();
  else if(ui.mode==='listen')units();else surface();
  rackTick();
  const name=titleName();if($('modelBtn').textContent!==name)$('modelBtn').textContent=name;
  if($('rack').hidden===!rk.why)backKey();   // not while a page change is still stepping out
  // An open list in the sheet would close if it were redrawn under the pointer.
  const f=document.activeElement;
  if(!$('sheet').hidden&&!(f?.tagName==='SELECT'&&$('sheet').contains(f)))sheet();
}
S.subscribe(render);
// The window is closing while Hold to compare is down: the speaker goes back to the program as it is.
addEventListener('pagehide',()=>endHold());
S.onAuditionError=err=>{ui.hold=false;toast(err);render();};

// ---- settings: one SETTINGS key whose contents follow the page --------------------------------------
// On the Speakers page (the app's home) it holds the app's settings: appearance, updates, help. On a
// speaker's page it holds that speaker's: beeps, auto shutdown, and its widget (buttons and "Let the
// phone app connect"). Nothing appears in both. It updates in place, so switches and the picker animate.
const BEEPS=['Power','Pairing','Low battery','Max volume','Party (TWS)'];
const sw=(on,label,hint,act,dis)=>`<button class="dv-sw" data-dv="${act}" role="switch" aria-checked="${on}"${dis?' disabled':''}><span><b>${label}</b><small>${hint}</small></span><i></i></button>`;
function speakerSheet(){
  const s=S.speaker,st=S.settings,can=S.linked(),connecting=s.status==='connecting',r=M.dropReason(s.reason,s.error);
  // One line under the name: firmware, and why there is no link when there is none.
  const facts=[s.firmware?`Firmware ${esc(s.firmware)}`:'',can?'':connecting?'Connecting':esc(r.word),!can&&s.lastSeen?`last seen ${lastSeen(s.lastSeen)}`:''].filter(Boolean).join(' · ');
  return `<section><h4 class="engr">${esc(titleName())}</h4>
      ${facts?`<p class="dv-sub">${!can&&!connecting?'<span class="led orange"></span>':''}${facts}</p>`:''}
      ${!can&&!connecting&&r.hint?`<p class="dv-hint">${esc(r.hint)}</p>`:''}
      ${!can&&!connecting&&r.key==='retry'?'<div class="dv-row"><button class="mech" data-dv="retry">Try again</button></div>':''}
      <div class="dv-label">Beeps${can?'':' <small>last known</small>'}</div>
      <div class="dv-chips">${BEEPS.map((l,i)=>`<button class="chip" data-dv="p${i}" aria-pressed="${s.prompts?.[i]===1}"${!can||s.prompts?.[i]<0?' disabled':''}>${l}</button>`).join('')}</div>
      ${sw(s.autoShutdown===1,'Auto shutdown','Turns off when nothing has played for a while','auto',!can||s.autoShutdown<0)}
      ${sw(!!st.release,'Let the phone app connect','Frees this speaker shortly after the widget closes','release')}
    </section>
    <p class="dv-note">Rename, power off, factory reset and firmware updates: use the Tribit app.</p>
    <p class="dv-note dv-more"><button class="dv-link" data-dv="appsettings">App settings ›</button><small>Appearance, widget, tray, updates</small></p>`;
}
// The widget: what it shows, and the two things Windows needs before it appears. Its six buttons are
// the first six programs; this is the one place that says so (and that the bar is the first twelve).
function widgetSection(){
  const st=S.settings,dev=st.devMode===true,added=st.widgetAdded===true;
  return `<section><h4 class="engr">Widget</h4>
    <p class="dv-sub">The widget and the tray menu show your first six programs. The program bar shows the first twelve.</p>
    ${added?'':`<div class="steps">
      <div class="step${dev?' done':''}"><span class="no">1</span><div><b>Turn on Developer Mode</b><small>Windows shows widgets from apps outside the Microsoft Store only while this is on.</small>${dev?'<span class="ok">On</span>':'<button class="mech" data-dv="developer">Open Windows settings</button>'}</div></div>
      <div class="step"><span class="no">2</span><div><b>Add the widget</b><small>Press Win + W, choose + Add widgets, then XSound&nbsp;Plus&nbsp;2.</small></div></div>
    </div>`}</section>`;
}
// Updates: what the host reports (updates.hpp), as one line and at most two keys.
function updateRow(){
  const st=S.settings,u=st.update||{},v=u.version?esc(u.version):'';
  const status={checking:['Checking…',true],current:['Up to date',false],failed:['Couldn’t check. Try again',false],dev:['Development build',true],
    available:[`${v} is available`,false],downloading:['Downloading…',true],unverified:['Update didn’t verify. Try again',false],stalled:['Update didn’t start. Try again',false],installing:['Installing…',true]}[u.state];
  const check=`<button class="mech" data-dv="check"${status?.[1]?' disabled':''}>${status?status[0]:'Check now'}</button>`;
  const install=u.state==='available'||u.state==='unverified'||u.state==='stalled'?`<button class="mech go" data-dv="install">Update to ${v}</button>`:'';
  return `${sw(st.checkUpdates!==false,'Check for updates','Once a day, from this project’s GitHub releases. Nothing about you is sent.','updates')}
    <div class="dv-row dv-update"><span class="dv-ver">Version ${esc(st.version||'')}</span>${install}${u.state==='available'?'':check}</div>`;
}
// Start with Windows: the package's startup task, as Windows reports it (app.cpp query_startup).
function startupRow(){
  const v=String(S.settings.startup||'unknown'),on=v==='on'||v==='policy-on';
  const hint={'user-off':'Turned off in Windows Settings > Apps > Startup. Turn it on there first.','policy-on':'Set by your organisation.','policy-off':'Turned off by your organisation.',
    unavailable:'Only in the installed app.',unknown:'Checking…'}[v]||'Opens in the tray when you sign in, so the widget is ready.';
  return sw(on,'Start with Windows',hint,'startup',v.startsWith('policy')||v==='unavailable'||v==='unknown');
}
function appSheet(){
  const st=S.settings;
  return `<section><h4 class="engr">Appearance</h4><div class="dv-seg" role="group" aria-label="Appearance">${[['light','Light'],['dark','Dark'],['device','Windows']].map(([v,l])=>
      `<button data-theme-choice="${v}" aria-pressed="${Theme.choice===v}">${l}</button>`).join('')}</div></section>
    ${widgetSection()}
    <section><h4 class="engr">Window</h4>
      ${sw(!!st.tray,'Close to tray','Closing keeps Tribit Connect in the notification area. Quit from its menu there.','tray')}
      ${startupRow()}
    </section>
    <section><h4 class="engr">Updates</h4>${updateRow()}</section>
    <section><h4 class="engr">Help</h4>
      ${sw(!!st.logging,'Diagnostic log','Only needed for troubleshooting.','logging')}
      <div class="dv-remove">${ui.removing?`<p>Remove all profiles and settings from this PC?</p><div class="dv-row"><button class="mech danger" data-dv="remove-yes">Remove</button><button class="mech" data-dv="remove-no">Keep</button></div>`:
        `<button class="dv-link" data-dv="remove">Remove my data…</button>`}</div>
      <p class="dv-note"><button class="dv-link" data-dv="submit">Another Tribit speaker? Help add it</button></p>
    </section>`;
}
// Which settings SETTINGS shows: the app's on the Speakers page, the speaker's everywhere else.
const sheetKind=()=>rk.why?'app':'speaker';
function sheet(){
  const el=$('sheet'),kind=sheetKind();el.setAttribute('aria-label',kind==='speaker'?`${titleName()} settings`:'Tribit Connect settings');
  if(el.dataset.kind!==kind){el.dataset.kind=kind;el.replaceChildren();el.classList.remove('swap');void el.offsetWidth;el.classList.add('swap');}
  morph(el,kind==='speaker'?speakerSheet():appSheet());
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
function toggleSheet(open){
  const el=$('sheet');clearTimeout(toggleSheet.t);el.classList.remove('closing');
  $('deviceBtn').setAttribute('aria-expanded',open);
  if(open){ui.wpick=null;ui.removing=false;el.hidden=false;sheet();return;}
  if(el.hidden)return;el.classList.add('closing');toggleSheet.t=setTimeout(()=>{el.hidden=true;el.classList.remove('closing');},S.reduce()?0:S.motion('t-norm'));
}

// ---- actions --------------------------------------------------------------------------------------
function toast(text,action){
  ui.toast?.remove();const t=document.createElement('div');t.className='toast';t.setAttribute('role','status');
  t.innerHTML=esc(text)+(action?`<button>${esc(action.label)}</button>`:'');
  if(action)t.querySelector('button').onclick=()=>{action.run();t.remove();};
  document.body.append(t);ui.toast=t;setTimeout(()=>{t.classList.add('out');setTimeout(()=>t.remove(),S.motion('t-norm'));},action?7000:3200);
}
// A program pressed without a link: say why, and offer Try again only when it can help.
function notLinked(){const r=M.dropReason(S.speaker.reason,S.speaker.error);
  const text={busy:'The speaker’s controls are busy',off:'The speaker is off or out of range',bluetooth:'Bluetooth is unavailable'}[S.speaker.reason]||'The speaker is not connected';
  toast(text,r.key==='retry'?{label:'Try again',run:()=>S.send({type:'retry'})}:undefined);}
// A key whose own write is still on its way is not sent again. Writes queue up, so several keys can
// be waiting at once; each blinks until its own write is done.
async function play(p){if(ui.pending.has(p.key))return;ui.pending.add(p.key);render();const r=await S.apply(p);ui.pending.delete(p.key);render();if(!r.ok)toast(r.error);}
const raw=bands=>({preset:M.CUSTOM,bands});
// ---- the editor is live ---------------------------------------------------------------------------
// ui.opened is the program as it was when it came onto the plate ({key, bands}, and for a copy just
// made from a factory program, that program's `preset`). Undo changes returns to it; Hold to compare
// plays it while the key is down.
function openProgram(key){const p=S.find(key);ui.opened=p?{key,bands:(p.factory?S.bandsOf(p):p.bands).slice()}:null;ui.hold=false;}
// The program on the plate is the one the speaker plays.
function hear(p){if(p&&!p.foreign&&S.linked()&&S.active()!==p.key)play(p);}
function endHold(){
  if(!ui.hold)return;ui.hold=false;
  const p=editing(),b=editBands();if(p&&b&&S.linked())S.audition(p.factory?{preset:p.id}:raw(b));
}
function undoChanges(){
  const p=editing(),o=ui.opened;if(!p||p.factory||!o||o.key!==p.key||M.sameBands(o.bands,p.bands))return;
  S.setBands(p.key,o.bands.slice());if(S.linked())S.audition(raw(o.bands));lastDisplay=null;render();
}
// Lights that hold the program just edited are written again, so a light never plays an older
// version of it. (Rewrite on the light's module does the same by hand if this could not.)
async function syncLights(key){
  if(!key||!S.linked())return;
  for(let i=0;i<3;i++){const v=S.slots()[i];if(v.key===key&&v.status==='stale')await S.assign(i,key);}
}
// Editing plays every change on the speaker, so it needs the speaker.
function enterEdit(key){
  if(!S.linked()){notLinked();return;}
  const a=key?S.find(key):playing();
  const target=a&&!a.foreign?a:S.programs()[0];
  S.transition(()=>{ui.mode='edit';ui.edit=target.key;openProgram(target.key);ui.choosing=null;lastDisplay=null;$('app').classList.add('editing');render();hear(target);});
}
function leaveEdit(){endHold();const was=ui.edit;S.transition(()=>{ui.mode='listen';ui.hot=-1;$('app').classList.remove('editing');render();syncLights(was);});}
function load(key){if(key===ui.edit||!S.find(key))return;endHold();const was=ui.edit;ui.edit=key;openProgram(key);render();hear(S.find(key));syncLights(was);}
function setBand(i,v){
  let p=editing();if(!p||!S.linked())return;
  ui.hold=false;if(ui.armed)disarm();
  v=Math.max(-8,Math.min(8,Math.round(v)));
  const b=(p.factory?S.bandsOf(p):p.bands).slice();if(b[i]===v)return;
  // A factory program is fixed in the speaker: the first change makes a copy, and the copy is edited.
  if(p.factory){const from=p,key=S.create({from:p.key});ui.edit=key;ui.opened={key,bands:b.slice(),preset:from.id};p=S.find(key);toast(`Made ${p.name}, a copy you can change`);}
  b[i]=v;S.setBands(p.key,b);S.audition(raw(b));   // queued: only the newest curve is sent
  surface();bank();
}
// "+ new" starts a flat program after the current one; DUPLICATE is the way to copy one.
function newProfile(){
  const cur=ui.mode==='edit'?editing():playing();
  const key=S.create({name:'New program',after:cur&&!cur.foreign?cur.key:null});
  const editing_=ui.mode==='edit';
  if(editing_)load(key);else enterEdit(key);
  setTimeout(()=>{if(ui.edit===key){$('pname').focus();$('pname').select();}},editing_?0:560);
}
// The EQ button on the drawing does what the real one does: the next light's program.
function nextLight(){
  if(!S.linked()){notLinked();return;}
  const i=(S.lit()+1)%3,v=S.slots()[i],p=v.key&&v.key!=='custom'?S.find(v.key):null;
  if(p){play(p);return;}
  const hw=S.speaker.buttons?.[i];if(hw&&hw.preset===M.CUSTOM&&Array.isArray(hw.bands))S.audition(raw(hw.bands));
}
async function assign(i,key){
  const p=S.find(key);if(!p||p.foreign)return;
  const r=await S.assign(i,key);toast(r.ok?`${Speaker.NAMES[i]} now selects ${p.name}`:r.error);
}
// Right-clicking a record or a program key opens a small menu. Delete offers Undo. A factory program
// can be opened and copied, nothing else.
function menu(key,x,y){
  const p=S.find(key),m=$('ctx');if(!p)return;m.dataset.key=key;
  m.innerHTML=`<button data-ctx="edit" role="menuitem">Edit</button><button data-ctx="dup" role="menuitem">Duplicate</button>${p.factory?'':
    '<span class="sep"></span><button data-ctx="del" class="danger" role="menuitem">Delete</button>'}`;
  m.hidden=false;
  m.style.left=Math.max(8,Math.min(x,innerWidth-m.offsetWidth-8))+'px';m.style.top=Math.max(8,Math.min(y,innerHeight-m.offsetHeight-8))+'px';
  m.querySelector('button').focus({preventScroll:true});
}
document.addEventListener('contextmenu',e=>{
  const r=e.target.closest('.rec[data-lkey],.pkey[data-key]');if(!r){hideCtx();return;}e.preventDefault();
  menu(r.dataset.lkey||r.dataset.key,e.clientX,e.clientY);
});
function hideCtx(){$('ctx').hidden=true;}
$('libbody').addEventListener('scroll',hideCtx);
// Delete takes two presses: the first arms it (pressed in, red lamp, CONFIRM; in the menu, "Delete …?"),
// the second within ARM_MS deletes. Anything else, or waiting, disarms it.
const ARM_MS=3000;
function armDelete(key){ui.armed=key;clearTimeout(armDelete.t);armDelete.t=setTimeout(disarm,ARM_MS);if(ui.mode==='edit')surface();}
function disarm(){if(!ui.armed)return;ui.armed=null;clearTimeout(armDelete.t);if(ui.mode==='edit')surface();
  const c=$('ctx').querySelector('[data-ctx=del].armed');if(c){c.classList.remove('armed');c.textContent='Delete';}}
// Deleting keeps Undo; a program open in the editor hands over to its neighbour.
function removeProfile(key){
  const p=S.find(key);if(!p||p.factory)return;
  const i=S.programs().findIndex(x=>x.key===key),open=ui.mode==='edit'&&ui.edit===key;
  if(open)endHold();
  const undo=S.remove(key),left=S.programs();
  if(open){ui.edit=null;load(left[Math.min(i,left.length-1)].key);}
  render();toast(`Deleted ${p.name}`,{label:'Undo',run:()=>{S.undo(undo);if(open&&ui.mode==='edit')load(undo.profile.key);render();}});
}
document.addEventListener('click',async e=>{
  const t=e.target;
  const cx=t.closest('[data-ctx]');
  // Any press other than the armed Delete itself disarms it.
  if(ui.armed&&!t.closest('[data-do=del]')&&!(cx&&cx.dataset.ctx==='del'))disarm();
  if(cx&&cx.dataset.ctx==='del'&&!cx.classList.contains('armed')){const p=S.find($('ctx').dataset.key);
    if(p){cx.classList.add('armed');cx.textContent=`Delete ${p.name}?`;armDelete(p.key);}return;}
  if(cx){const key=$('ctx').dataset.key,what=cx.dataset.ctx;hideCtx();if(what==='del')disarm();
    if(what==='edit'){toggleLibrary(false);ui.mode==='edit'?load(key):enterEdit(key);}
    else if(what==='dup'){const k=S.create({from:key});
      if(ui.mode==='edit'&&ui.edit===key&&!ui.library){load(k);$('pname').focus();$('pname').select();}else toast(`Made ${S.find(k).name}`);}
    else if(what==='del')removeProfile(key);
    return;}
  if(!t.closest('#ctx'))hideCtx();
  const win=t.closest('[data-win]');if(win){Bridge.send({type:'window',action:win.dataset.win});return;}
  // ‹ Speakers: to the app's home, the Speakers page. (There is no way back from it but Open.)
  if(t.closest('#backBtn')){toggleSheet(false);if(!rk.why)openRack('manual');return;}
  const op=t.closest('[data-open]');
  if(op){const a=op.dataset.open;
    // The speaker in use: back to it. Another: switch, and the page closes once it links.
    if(a===S.speakers?.address&&S.linked()){S.choose(a,false);closeRack();}
    else{rk.why='switch';rk.dropped=false;rk.switchAt=Date.now();S.choose(a);}
    return;}
  const help=t.closest('[data-help]');if(help){S.send({type:'openSubmit',model:help.dataset.help});return;}
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
    else if(dv==='retry')S.send({type:'retry'});else if(dv==='bt')S.send({type:'openBluetooth'});else if(dv==='submit')S.send({type:'openSubmit'});
    else if(dv==='check')S.checkUpdate();else if(dv==='install')S.installUpdate();
    else if(dv==='updates')S.setCheckUpdates(S.settings.checkUpdates===false);
    else if(dv==='tray')S.setTray(!S.settings.tray);
    else if(dv==='developer')S.send({type:'openDeveloper'});
    // The app's own settings live on the Speakers page: go there and open them.
    else if(dv==='appsettings'){openRack('manual');setTimeout(()=>{if(rk.why)toggleSheet(true);},S.reduce()?0:380);}
    else if(dv==='startup')S.setStartup(S.settings.startup!=='on');
    return;}
  if(t.closest('#deviceBtn')){toggleSheet($('sheet').hidden);return;}
  // A click outside Settings only closes it; it does not also press what was under it.
  if(!$('sheet').hidden&&!t.closest('#sheet')){ui.wpick=null;toggleSheet(false);return;}
  if(t.closest('#editKey')){ui.library=false;ui.mode==='edit'?leaveEdit():enterEdit();return;}
  const rw=t.closest('[data-rewrite]');if(rw){const i=+rw.dataset.rewrite;assign(i,S.slots()[i].key);return;}
  if(t.closest('#spk .spk-eq')){nextLight();return;}
  const arm=t.closest('[data-arm]');if(arm){const i=+arm.dataset.arm;ui.choosing=ui.choosing===i?null:i;render();return;}
  if(t.closest('[data-new]')){newProfile();return;}
  if(t.closest('[data-library]')){toggleLibrary(!ui.library);return;}
  const lc=t.closest('[data-lkey]');
  if(lc){const p=S.find(lc.dataset.lkey);
    if(ui.choosing!==null){const i=ui.choosing;ui.choosing=null;toggleLibrary(false);assign(i,p.key);return;}
    if(ui.mode==='edit'){toggleLibrary(false);load(p.key);return;}
    if(!S.linked()){notLinked();return;}
    play(p);return;}
  const k=t.closest('[data-key]');
  if(k){k.classList.add('down');setTimeout(()=>k.classList.remove('down'),S.motion('t-press')+40);
    const p=S.find(k.dataset.key);
    if(ui.choosing!==null){const i=ui.choosing;ui.choosing=null;render();assign(i,p.key);return;}
    if(ui.mode==='edit'){load(p.key);return;}
    if(!S.linked()){notLinked();return;}
    play(p);return;}
  const d=t.closest('[data-do]')?.dataset.do;if(!d)return;
  const p=editing();if(!p||ui.mode!=='edit')return;
  if(d==='undo')undoChanges();
  // Compare: lit, the speaker plays the program as it was; pressed again, as it is.
  else if(d==='compare'){if(ui.hold){endHold();surface();return;}
    const o=ui.opened;if(!o||!S.linked())return;ui.hold=true;S.audition(o.preset!=null?{preset:o.preset}:raw(o.bands));surface();}
  else if(d==='dup'){endHold();const k=S.create({from:p.key});load(k);$('pname').focus();$('pname').select();}
  else if(d==='flat'){const flat=B.freqs.map(()=>0);endHold();S.setBands(p.key,flat);if(S.linked())S.audition(raw(flat));lastDisplay=null;render();}
  else if(d==='del'){if(ui.armed===p.key){disarm();removeProfile(p.key);}else armDelete(p.key);}
});
$('pname').addEventListener('change',e=>{const p=editing();if(p&&!p.factory)S.rename(p.key,e.target.value);});
$('pname').addEventListener('keydown',e=>{if(e.key==='Enter'||e.key==='Escape')e.target.blur();});
document.addEventListener('keydown',e=>{
  if(typeof e.key!=='string')return;   // autofill and some input methods send key events without a key
  // Escape closes what is on top first: the menu, then Settings, then the page under them.
  // A key press other than Enter or Space on the armed Delete disarms it (Escape included).
  if(ui.armed&&!((e.key==='Enter'||e.key===' ')&&e.target.closest?.('[data-do=del],[data-ctx=del]'))){disarm();if(e.key==='Escape')return;}
  // Escape closes what is on top first: the menu, then Settings, the library, a light's pick, and last
  // the editor (as DONE). Escape while renaming only ends the rename; on the Speakers page it does nothing.
  if(e.key==='Escape'){if(!$('ctx').hidden){hideCtx();return;}if(!$('sheet').hidden){toggleSheet(false);return;}if(rk.why)return;if(ui.library){toggleLibrary(false);return;}if(ui.choosing!==null){ui.choosing=null;render();return;}
    if(ui.mode==='edit'&&e.target.id!=='pname'&&!drag.started){leaveEdit();return;}}
  if((e.key==='Enter'||e.key===' ')&&e.target.closest?.('#spk .spk-eq')){e.preventDefault();nextLight();return;}
  const uk=e.altKey&&(e.key==='ArrowLeft'||e.key==='ArrowRight')&&e.target.closest?.('.pkey.prog');
  if(uk){e.preventDefault();const key=uk.dataset.key,i=S.programs().findIndex(p=>p.key===key);S.move(key,i+(e.key==='ArrowRight'?1:-1));
    $('keys').querySelector(`[data-key="${key}"]`)?.focus();return;}
  // Undo and the fader keys belong to the editor while it is on screen.
  const inEditor=ui.mode==='edit'&&!rk.why&&!ui.library;
  if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='z'&&inEditor&&e.target.id!=='pname'){e.preventDefault();undoChanges();return;}
  const s=inEditor&&e.target.closest?.('.slot');if(!s)return;
  const i=+s.dataset.band,b=editBands()[i];
  const step={ArrowUp:1,ArrowDown:-1,PageUp:4,PageDown:-4}[e.key];
  if(step!=null){e.preventDefault();setBand(i,b+step);}
  else if(e.key==='Home'||e.key==='End'){e.preventDefault();setBand(i,e.key==='Home'?8:-8);}
  else if(e.key==='0'){setBand(i,0);}
  else if(e.key==='ArrowRight'||e.key==='ArrowLeft'){e.preventDefault();$('channels').querySelector(`[data-band="${Math.max(0,Math.min(N-1,i+(e.key==='ArrowRight'?1:-1)))}"]`)?.focus();}
});
// The focus ring shows only while moving around with Tab; a press with the mouse or a finger hides it.
// (The browser's own rule also drew it on a clicked button as soon as any key was pressed.)
addEventListener('keydown',e=>{if(e.key==='Tab')document.documentElement.classList.add('kbd');},true);
addEventListener('pointerdown',()=>document.documentElement.classList.remove('kbd'),true);
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
// Dragging a program key rearranges the bar: the key follows the pointer along its row, never off it,
// and the others slide aside. (A light is given a program by pressing its key, then the program.)
const drag={el:null,key:null,x0:0,y0:0,started:false,row:null,from:0,to:0,step:0,others:[]};
function startDrag(e){
  const d=drag,el=d.el;d.started=true;try{el.setPointerCapture(e.pointerId);}catch{}
  el.classList.add('held');d.row=el.parentElement;d.row.classList.add('sorting');
  const keys=[...d.row.querySelectorAll('.pkey.prog:not([hidden]):not(.visiting)')];d.from=d.to=keys.indexOf(el);d.others=keys;
  d.step=keys.length>1?keys[1].getBoundingClientRect().left-keys[0].getBoundingClientRect().left:el.getBoundingClientRect().width;
}
function moveDrag(e){
  const d=drag,el=d.el,n=d.others.length,dx=Math.max(-d.from*d.step,Math.min((n-1-d.from)*d.step,e.clientX-d.x0));
  el.style.transform=`translateX(${dx}px) translateY(-4px)`;
  const to=Math.max(0,Math.min(n-1,d.from+Math.round(dx/d.step)));if(to===d.to)return;d.to=to;
  d.others.forEach((k,i)=>{if(k===el)return;const shift=i>d.from&&i<=to?-d.step:i<d.from&&i>=to?d.step:0;k.style.transform=shift?`translateX(${shift}px)`:'';});
}
function endDrag(commit){
  const d=drag,el=d.el;if(!el)return;
  const done=()=>{el.classList.remove('held');el.removeAttribute('style');d.row?.classList.remove('sorting');
    d.others.forEach(k=>k.style.transform='');Object.assign(d,{el:null,started:false,row:null,others:[]});
    $('keys').dataset.html='';bank();};
  if(!commit||!d.started){done();return;}
  // Settle the key into its new slot, then save the order (the redraw renumbers the keys). The bar is
  // the first programs in order, so a place on the bar is the same place in the list.
  const key=d.key,to=d.to,from=d.from;el.style.transition='transform .18s var(--ease)';el.style.transform=`translateX(${(to-from)*d.step}px)`;
  setTimeout(()=>{done();if(to!==from)S.move(key,to);},S.reduce()?0:180);
}
document.addEventListener('pointerdown',e=>{
  if(e.button!==0||drag.el)return;const k=e.target.closest('.pkey.prog[data-key]');if(!k||k.classList.contains('visiting'))return;
  Object.assign(drag,{el:k,key:k.dataset.key,x0:e.clientX,y0:e.clientY,started:false,others:[]});
});
document.addEventListener('pointermove',e=>{
  if(!drag.el)return;
  if(!drag.started){if(Math.hypot(e.clientX-drag.x0,e.clientY-drag.y0)<6)return;startDrag(e);}
  e.preventDefault();moveDrag(e);
});
document.addEventListener('pointerup',()=>{
  if(!drag.el)return;if(!drag.started){drag.el=null;return;}
  // The click that follows a drag must not also play the program.
  const swallow=ev=>{ev.stopImmediatePropagation();ev.preventDefault();};
  addEventListener('click',swallow,{capture:true,once:true});setTimeout(()=>removeEventListener('click',swallow,true),0);
  endDrag(true);
});
document.addEventListener('pointercancel',()=>{if(drag.started)endDrag(false);else drag.el=null;});
document.addEventListener('keydown',e=>{if(e.key==='Escape'&&drag.started)endDrag(false);});
addEventListener('resize',()=>{if(ui.mode==='listen')conduits();const row=document.querySelector('.userrow');if(row)fit(row);});
// Re-measure once the speaker and modules have settled after a mode change.
$('rig').addEventListener('animationend',e=>{if(e.target.classList.contains('spkwrap'))conduits();});
render();
S.listSpeakers();   // two supported speakers and no choice yet: the Speakers page asks once
// #edit opens the editor on the first saved profile, #device the sheet (reviews and screenshots).
// #settings opens SETTINGS once the launch has settled (reviews and screenshots).
if(location.hash==='#settings'||location.hash==='#device'){const off=S.subscribe(()=>{if(S.settled&&!$('app').classList.contains('booting')){off();setTimeout(()=>toggleSheet(true),400);}});}
// Reviews and screenshots: #speakers opens the Speakers page as the back key does; ?edit opens the
// editor first, so the two can be checked together.
if(/[?&]edit\b/.test(location.search)||location.hash==='#speakers'){const off=S.subscribe(()=>{if(!S.settled||$('app').classList.contains('booting'))return;off();
  const k=S.custom()[0]?.key;if(/[?&]edit\b/.test(location.search)&&k&&!rk.why){ui.mode='edit';ui.edit=k;openProgram(k);$('app').classList.add('editing');render();}
  if(location.hash==='#speakers')setTimeout(()=>openRack('manual'),300);});}
// A library that could not be read is said once; the file itself was set aside, not replaced.
{const off=S.subscribe(()=>{if(!S.ready())return;off();if(S.libraryError)toast('Your profiles could not be read. The file was kept as profiles.bad in the data folder.',{label:'OK',run(){}});});}
// #library opens All programs.
if(location.hash==='#library'){const off=S.subscribe(()=>{if(!S.settled||$('app').classList.contains('booting'))return;off();setTimeout(()=>toggleLibrary(true),300);});}
if(location.hash==='#edit'){const go=()=>{const k=S.custom()[0]?.key;if(k){ui.mode='edit';ui.edit=k;openProgram(k);$('app').classList.add('editing');render();}};
  if(S.ready())go();else{const off=S.subscribe(()=>{if(S.ready()){off();go();}});}}
})();
