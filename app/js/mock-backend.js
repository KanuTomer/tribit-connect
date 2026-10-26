// A simulated XSound Plus 2 for working on the UI in a browser. It answers the same messages as
// the native host, with realistic delays, and a debug strip (press `) changes its state.
(function(root){
'use strict';
if(root.chrome&&root.chrome.webview)return;
const CUSTOM=0xFE,STORE='tribit-connect.mock.library';
const sim={
  speaker:{connected:true,status:'connected',error:'',name:'XSound Plus 2',firmware:'0.3.1',battery:90,
    preset:1,bands:[0,0,0,0,0,0,0,0,0],buttons:[{preset:0},{preset:1},{preset:3}],
    prompts:[1,1,1,1,1],autoShutdown:1,lastSeen:Date.now()},
  settings:{release:false,logging:false},
  latency:260,failNext:false,
};
// ?offline starts disconnected (screenshots of the offline state).
if(/[?&]offline/.test(location.search)){sim.speaker.connected=false;sim.speaker.status='disconnected';}
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
function publish(){emit({type:'state',speaker:structuredClone(sim.speaker),settings:{...sim.settings},library:library()});}
const delay=ms=>new Promise(r=>setTimeout(r,ms));
// Writes behave like the speaker: refused while disconnected, acknowledged after a short delay.
async function write(id,change){
  const s=sim.speaker;
  if(!s.connected){emit({type:'result',id,ok:false,error:'The speaker is not connected.'});return;}
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
    case 'removeData':try{localStorage.removeItem(STORE);}catch{}break;
    case 'saveLibrary':try{localStorage.setItem(STORE,JSON.stringify(m.library));}catch{}break;
    case 'retry':connect(true);break;
    case 'openBluetooth':console.info('[mock] would open Windows Bluetooth settings');break;
    case 'window':console.info('[mock] window',m.action);break;
  }
}
async function connect(on){
  const s=sim.speaker;
  if(on){s.status='connecting';publish();await delay(900);s.connected=true;s.status='connected';s.error='';s.lastSeen=Date.now();}
  else{s.connected=false;s.status='disconnected';}
  publish();renderDebug();
}
// ---- Debug strip -------------------------------------------------------------------------
let strip;
function renderDebug(){
  if(!strip)return;const s=sim.speaker;
  strip.innerHTML=`<b>mock speaker</b>
    <button data-a="link">${s.connected?'disconnect':'connect'}</button>
    <button data-a="error">${s.status==='unavailable'?'clear error':'bluetooth error'}</button>
    <button data-a="fail" ${sim.failNext?'class=on':''}>fail next write</button>
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
    if(a==='error'){if(s.status==='unavailable'){connect(true);}else{s.connected=false;s.status='unavailable';s.error='Bluetooth is turned off.';publish();renderDebug();}}
    if(a==='fail'){sim.failNext=!sim.failNext;renderDebug();}
    if(a==='press'&&s.connected){const lights=s.buttons;const i=(lights.findIndex(b=>b.preset===s.preset&&(b.preset!==CUSTOM||JSON.stringify(b.bands)===JSON.stringify(s.bands)))+1)%3;
      s.preset=lights[i].preset;s.bands=lights[i].bands?lights[i].bands.slice():[0,0,0,0,0,0,0,0,0];publish();}
    if(a==='music'){playMusic(!music);renderDebug();}
    if(a==='reset'){localStorage.removeItem(STORE);location.reload();}});
  strip.addEventListener('input',e=>{const a=e.target.dataset.a;
    if(a==='battery'){sim.speaker.battery=+e.target.value;publish();}
    if(a==='latency')sim.latency=+e.target.value;
    renderDebug();});
}
root.MockBackend={connect(fn){emit=fn;setTimeout(()=>{if(!music)emit({type:'spectrum',playing:false,listening:true,bands:[]});},200);if(/[?&]music/.test(location.search))setTimeout(()=>playMusic(true),300);if(document.readyState==='loading')addEventListener('DOMContentLoaded',setupDebug);else setupDebug();},receive};
})(this);
