// A simulated XSound Plus 2 for working on the UI in a browser. It answers the same messages as
// the native host, with realistic delays, and a debug strip (press `) changes its state.
(function(root){
'use strict';
if(root.chrome&&root.chrome.webview)return;
const CUSTOM=0xFE,STORE='tribit-connect.mock.library';
const sim={
  speaker:{connected:true,status:'connected',error:'',name:'XSound Plus 2',firmware:'0.3.1',battery:90,
    preset:1,bands:[0,0,0,0,0,0,0,0,0],buttons:[{preset:0},{preset:1},{preset:3}],
    prompts:[1,1,1,1,1],autoShutdown:1,lastSeen:Date.now(),settled:false,reason:''},
  settings:{release:false,logging:false,chosen:false,checkUpdates:true,tray:false,startup:'off',devMode:/[?&]devmode/.test(location.search),widgetAdded:/[?&]widget/.test(location.search),version:'0.9.7',update:{state:'idle',version:''}},
  latency:260,failNext:false,
};
// Paired Tribit speakers for the Speakers page. ?speakers=two adds a second XSound Plus 2,
// ?speakers=none has nothing paired.
const PAIRED={
  one:[{name:'Tribit XSound Plus 2',model:'XSound Plus 2',address:'00:11:22:33:44:01',connected:true,supported:true},
       {name:'Tribit StormBox Flow',model:'StormBox Flow',address:'00:11:22:33:44:03',connected:false,supported:false}],
};
PAIRED.two=[PAIRED.one[0],{name:'XSound Plus 2 (Study)',model:'XSound Plus 2',address:'00:11:22:33:44:02',connected:true,supported:true},PAIRED.one[1]];
PAIRED.none=[];
// ?speakers=six: a crowded case, with long Windows names
PAIRED.six=[...PAIRED.two,
  {name:'Tribit StormBox Micro 2 — Kitchen Counter',model:'StormBox Micro 2',address:'00:11:22:33:44:04',connected:false,supported:false},
  {name:'Tribit XSound Mega',model:'XSound Mega',address:'00:11:22:33:44:05',connected:true,supported:false},
  {name:"Kanu's XSound Plus 2 Bedroom",model:'XSound Plus 2',address:'00:11:22:33:44:06',connected:false,supported:true}];
// ?showcase (with store.js's showcase library): the speaker plays Night Desk, which is also on its first light.
if(/[?&]showcase/.test(location.search)){const nd=[3,4,2,0,-1,-1,0,1,2];Object.assign(sim.speaker,{preset:CUSTOM,bands:nd.slice(),buttons:[{preset:CUSTOM,bands:nd.slice()},{preset:1},{preset:3}]});}
sim.settings.startup=(location.search.match(/[?&]startup=([\w-]+)/)||[])[1]||sim.settings.startup;sim.paired=structuredClone(PAIRED[(location.search.match(/[?&]speakers=(\w+)/)||[])[1]]||PAIRED.one);
sim.address=sim.paired.find(d=>d.supported)?.address||'';
// States for screenshots: ?scene=compare (a moved fader, Compare on, a hovered channel) and ?scene=menu
// (a record's right-click menu).
{const scene=(location.search.match(/[?&]scene=(\w+)/)||[])[1],q=s=>document.querySelector(s);
 if(scene==='compare')setTimeout(()=>{const slot=q('#channels [data-band="3"]');if(!slot)return;slot.focus();
   for(let i=0;i<5;i++)slot.dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowUp',bubbles:true}));
   setTimeout(()=>{q('[data-do=compare]')?.click();q('#channels .chan')?.classList.add('hot');},900);},2500);
 if(scene==='menu')setTimeout(()=>{const r=q('.rec[data-lkey]');if(!r)return;const b=r.getBoundingClientRect();
   r.dispatchEvent(new MouseEvent('contextmenu',{bubbles:true,cancelable:true,clientX:b.left+b.width/2,clientY:b.top+b.height/2}));},2500);}
// ?offline starts disconnected (screenshots of the offline state).
// Like the host, the first attempt takes a moment and then settles (the page shows nothing about
// the speaker until it does).
const startOff=/[?&]offline/.test(location.search)||!sim.address;
sim.speaker.connected=false;sim.speaker.status='connecting';
setTimeout(()=>{const s=sim.speaker;s.settled=true;
  if(startOff){s.status='disconnected';s.reason='off';if(chosen())chosen().connected=false;}else{s.connected=true;s.status='connected';s.reason='';}
  publish();speakers();renderDebug();},700);
// The chosen speaker's entry follows the link.
const chosen=()=>sim.paired.find(d=>d.address===sim.address);
// seen: when this app last read each speaker (the host keeps it per address).
sim.paired.forEach((d,i)=>{d.seen=Date.now()-(i+1)*3600e3;});
if(chosen())sim.speaker.lastSeen=chosen().seen;
function speakers(){emit({type:'speakers',list:structuredClone(sim.paired),address:sim.address,chosen:sim.settings.chosen});}
let emit=()=>{};
// A stand-in for music this PC plays through the speaker (?music, or the strip's button).
let music=0;
function playMusic(on){
  clearInterval(music);music=0;
  if(!on){emit({type:'spectrum',playing:false,listening:true,bands:[]});return;}
  const lv=[.6,.65,.5,.45,.4,.35,.3,.22,.15];let t=0;
  music=setInterval(()=>{t++;const beat=t%8<2?.25:0;
    emit({type:'spectrum',playing:true,listening:true,bands:lv.map((v,i)=>Math.max(0,Math.min(1,v+(i<2?beat:0)+(Math.random()-.5)*.25)))});},50);
}
function library(){try{return JSON.parse(localStorage.getItem(STORE))||null;}catch{return null;}}
// ?badlibrary: a profiles file that exists but cannot be read (the page must not start a new one over it).
const BAD=/[?&]badlibrary/.test(location.search);
function publish(){emit({type:'state',speaker:structuredClone(sim.speaker),settings:{...sim.settings},library:BAD?null:library(),...(BAD?{libraryError:true}:{})});}
const delay=ms=>new Promise(r=>setTimeout(r,ms));
// Writes behave like the speaker: refused while disconnected, acknowledged after a short delay.
async function write(id,change){
  const s=sim.speaker;
  if(!s.connected){emit({type:'result',id,ok:false,error:'The speaker is not connected.'});return;}
  if(sim.mute){console.info('[mock] not answering',id);return;}   // the page's own timeout must end the wait
  s.status='updating';publish();
  await delay(sim.latency);
  if(sim.failNext){sim.failNext=false;s.status='connected';publish();emit({type:'result',id,ok:false,error:'The speaker did not confirm the change.'});renderDebug();return;}
  change(s);s.status='connected';s.lastSeen=Date.now();publish();emit({type:'result',id,ok:true});
}
function receive(m){
  const s=sim.speaker;
  switch(m.type){
    case 'ready':publish();break;
    case 'apply':write(m.id,s=>{s.preset=m.preset;s.bands=m.preset===CUSTOM?m.bands.slice():[0,0,0,0,0,0,0,0,0];});break;
    case 'assign':write(m.id,s=>{s.buttons=m.slots.map(x=>x.preset===CUSTOM?{preset:CUSTOM,bands:x.bands.slice()}:{preset:x.preset});});break;
    case 'setPrompt':write(m.id,s=>{s.prompts[m.index]=m.value?1:0;});break;
    case 'setAutoShutdown':write(m.id,s=>{s.autoShutdown=m.value?1:0;});break;
    case 'setRelease':sim.settings.release=!!m.value;publish();break;
    case 'setLogging':sim.settings.logging=!!m.value;publish();break;
    case 'removeData':try{localStorage.removeItem(STORE);}catch{}sim.settings.chosen=false;break;
    case 'saveLibrary':try{localStorage.setItem(STORE,JSON.stringify(m.library));}catch{}break;
    case 'retry':if(chosen()?.connected||s.reason==='busy')connect(true);break;
    case 'markChosen':sim.settings.chosen=true;publish();speakers();break;
    case 'setCheckUpdates':sim.settings.checkUpdates=!!m.value;publish();break;
    case 'setTray':sim.settings.tray=!!m.value;publish();break;
    // ?startup=user-off (or policy-on, policy-off, unavailable) shows what Windows can answer instead.
    case 'setStartup':setTimeout(()=>{if(sim.settings.startup==='on'||sim.settings.startup==='off')sim.settings.startup=m.value?'on':'off';publish();},300);break;
    // ?uptodate answers Check now with Up to date; otherwise 0.7.1 is offered.
    case 'checkUpdate':updateState('checking');setTimeout(()=>{if(/[?&]uptodate/.test(location.search)){updateState('current');setTimeout(()=>{if(sim.settings.update.state==='current')updateState('idle');},4000);}else updateState('available','0.7.3');},800);break;
    case 'installUpdate':updateState('downloading','0.7.1');setTimeout(()=>{updateState('installing','0.7.1');console.info('[mock] would run setup /update');},1200);break;
    case 'listSpeakers':speakers();break;
    case 'chooseSpeaker':{const d=sim.paired.find(x=>x.address===m.address&&x.supported);if(!d)break;
      sim.address=d.address;sim.settings.chosen=true;s.connected=false;s.status='disconnected';s.reason='off';s.lastSeen=d.seen;publish();if(d.connected)connect(true);break;}
    // A script error on the page is shown, so a broken mock is never mistaken for a blank design.
    case 'log':{const b=document.createElement('pre');b.textContent=m.text;b.style.cssText='position:fixed;left:8px;top:8px;z-index:99;max-width:70vw;white-space:pre-wrap;background:#b00020;color:#fff;font:12px/1.4 monospace;padding:8px 10px;border-radius:6px';document.body.append(b);break;}
    case 'openDeveloper':console.info('[mock] would open Windows developer settings');sim.settings.devMode=true;publish();break;
    case 'openBluetooth':console.info('[mock] would open Windows Bluetooth settings');break;
    case 'openSubmit':console.info('[mock] would open https://tribit-connect-submit.vercel.app/'+(m.model?'?model='+encodeURIComponent(m.model):''));break;
    case 'window':console.info('[mock] window',m.action);break;
  }
}
function updateState(state,version=''){sim.settings.update={state,version};publish();}
async function connect(on){
  const s=sim.speaker;
  const d=chosen();
  if(on){if(!d)return;s.status='connecting';publish();await delay(900);d.connected=true;s.connected=true;s.status='connected';s.error='';s.reason='';s.lastSeen=d.seen=Date.now();}
  else{s.connected=false;s.status='disconnected';s.reason='off';if(d)d.connected=false;}
  publish();speakers();renderDebug();
}
// ---- Debug strip -------------------------------------------------------------------------
let strip;
function renderDebug(){
  if(!strip)return;const s=sim.speaker;
  strip.innerHTML=`<b>mock speaker</b>
    <button data-a="link">${s.connected?'disconnect':'connect'}</button>
    <button data-a="glitch">1 s drop</button>
    <button data-a="error">${s.status==='unavailable'?'clear error':'bluetooth error'}</button>
    <button data-a="busy">controls busy</button>
    <button data-a="fail" ${sim.failNext?'class=on':''}>fail next write</button>
    <button data-a="mute" ${sim.mute?'class=on':''}>never answer</button>
    <label>battery <input data-a="battery" type="range" min="0" max="100" value="${s.battery}"> ${s.battery}%</label>
    <label>latency <input data-a="latency" type="range" min="0" max="2000" step="50" value="${sim.latency}"> ${sim.latency} ms</label>
    <button data-a="press">press EQ button</button>
    <button data-a="music" ${music?'class=on':''}>music</button>
    <button data-a="reset">clear library</button>`;
}
function setupDebug(){
  strip=document.createElement('div');strip.className='mock-strip';strip.hidden=!/[?&]debug/.test(location.search);document.body.append(strip);renderDebug();
  addEventListener('keydown',e=>{if(e.key==='`'){strip.hidden=!strip.hidden;}});
  strip.addEventListener('click',e=>{const a=e.target.dataset.a,s=sim.speaker;
    if(a==='link')connect(!s.connected);
    if(a==='glitch'&&s.connected){connect(false);setTimeout(()=>connect(true),1000);}
    if(a==='error'){if(s.status==='unavailable'){connect(true);}else{s.connected=false;s.status='unavailable';s.reason='bluetooth';s.error='Bluetooth is turned off.';publish();renderDebug();}}
    // Windows has the speaker, but another controller (the phone app) holds its controls.
    if(a==='busy'){s.connected=false;s.status='unavailable';s.reason='busy';s.error='The speaker did not accept the control connection.';publish();renderDebug();}
    if(a==='fail'){sim.failNext=!sim.failNext;renderDebug();}
    if(a==='mute'){sim.mute=!sim.mute;renderDebug();}
    if(a==='press'&&s.connected){const lights=s.buttons;const i=(lights.findIndex(b=>b.preset===s.preset&&(b.preset!==CUSTOM||JSON.stringify(b.bands)===JSON.stringify(s.bands)))+1)%3;
      s.preset=lights[i].preset;s.bands=lights[i].bands?lights[i].bands.slice():[0,0,0,0,0,0,0,0,0];publish();}
    if(a==='music'){playMusic(!music);renderDebug();}
    if(a==='reset'){localStorage.removeItem(STORE);location.reload();}});
  strip.addEventListener('input',e=>{const a=e.target.dataset.a;
    if(a==='battery'){sim.speaker.battery=+e.target.value;publish();}
    if(a==='latency')sim.latency=+e.target.value;
    renderDebug();});
}
// ?smoke runs tests/smoke.js: a scripted pass over the real controls, reported in a box on the page.
if(/[?&]smoke/.test(location.search))addEventListener('load',()=>{const t=document.createElement('script');t.src='tests/smoke.js';document.body.append(t);});
root.MockBackend={connect(fn){emit=fn;setTimeout(()=>{if(!music)emit({type:'spectrum',playing:false,listening:true,bands:[]});},200);if(/[?&]music/.test(location.search))setTimeout(()=>playMusic(true),300);if(document.readyState==='loading')addEventListener('DOMContentLoaded',setupDebug);else setupDebug();},receive};
})(this);
