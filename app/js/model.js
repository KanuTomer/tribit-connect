// Sound Lab state model: pure functions, no DOM. Runs in the app (classic script, so the
// mock works from file://) and under node for tests (module.exports).
(function(root){
'use strict';

// Band layout of the speaker's custom EQ, as labelled in the Tribit app: nine bands, ±8 dB in
// 1 dB steps (byte 8 = 0 dB). The app keeps its custom profiles locally; the speaker holds one.
const BANDS={freqs:[80,150,300,600,1200,2500,5000,9000,13000],range:8};

// Factory presets in the app's order. Shapes are traced from the Tribit app's graph (33 samples
// spanning the lowest to the highest band, heights 0..1000); they are drawings, not band data.
const FACTORY=[
  {id:0,name:'XBass Off',hint:'flat low end',trace:[472,565,643,704,746,748,713,652,583,535,513,498,491,487,480,474,467,463,461,470,502,548,583,593,589,576,565,559,561,578,604,639,678]},
  {id:1,name:'XBass',hint:'bass lift',trace:[465,546,611,663,698,700,676,628,574,535,511,493,483,476,470,465,461,461,461,463,480,504,522,526,524,517,511,509,509,517,533,548,567]},
  {id:3,name:'MaxSound',hint:'traditional',trace:[411,450,485,522,559,604,654,691,704,704,704,704,704,704,704,704,704,704,704,704,702,689,648,598,557,524,489,467,459,459,459,459,459]},
  {id:4,name:'Rock',hint:'bright top',trace:[580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,593,626,663,691,709,720,726,728,728,728,728,728]},
  {id:2,name:'Audiobook',hint:'voice band',trace:[461,461,461,461,461,467,528,626,715,752,752,752,752,752,752,752,752,752,752,746,715,663,613,578,548,524,504,489,476,470,465,463,461]},
  {id:5,name:'Jazz',hint:'warm low mid',trace:[726,672,628,596,580,604,657,709,728,728,728,728,728,707,654,602,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580,580]},
].map(p=>({...p,key:'f'+p.id,factory:true}));
const CUSTOM=0xFE;
// Trace height -> approximate dB, measured from the preset's median level. The app's vertical
// scale is not calibrated, so curves started from a factory preset are labelled as approximations.
const median=t=>{const s=t.slice().sort((a,b)=>a-b);return s[s.length>>1];};
FACTORY.forEach(p=>{p.base=median(p.trace);});
const traceDb=(h,p)=>(h-p.base)/38;

const clampBand=v=>Math.max(-BANDS.range,Math.min(BANDS.range,Math.round(v)));
const sameBands=(a,b)=>!!a&&!!b&&a.length===b.length&&a.every((v,i)=>v===b[i]);
const flat=()=>BANDS.freqs.map(()=>0);
let counter=0;
const newKey=()=>'p'+Date.now().toString(36)+(counter++).toString(36);

function emptyLibrary(){return{profiles:[],order:FACTORY.map(p=>p.key),links:{custom:null,slots:[null,null,null]}};}
// ---- one list of programs ------------------------------------------------------------------------
// Everything that can be played is a program, in one order the user arranges: their own profiles and
// the six factory presets alike (library.order holds the keys). The first BANK are the program bar's
// keys and the first WIDGET the widget's and the tray menu's buttons; nothing else decides either.
// The order is repaired on the way out, so a key missing from it (a new profile, an old library)
// still has a place: own profiles before the factory presets, which most people do not use.
function orderOf(library){
  const own=library.profiles.map(p=>p.key),all=new Set([...own,...FACTORY.map(p=>p.key)]),seen=new Set(),out=[];
  for(const k of Array.isArray(library.order)?library.order:[])if(all.has(k)&&!seen.has(k)){seen.add(k);out.push(k);}
  const firstFactory=out.findIndex(k=>/^f\d+$/.test(k)),missing=own.filter(k=>!seen.has(k));
  if(missing.length)out.splice(firstFactory<0?out.length:firstFactory,0,...missing);
  for(const f of FACTORY)if(!seen.has(f.key))out.push(f.key);
  return out;
}
const programs=library=>orderOf(library).map(k=>findProfile(library,k));
const numberOf=(library,key)=>{const i=orderOf(library).indexOf(key);return i<0?null:i+1;};

function findProfile(library,key){
  return FACTORY.find(p=>p.key===key)||library.profiles.find(p=>p.key===key)||null;
}
// Bands for a profile; factory presets give their traced approximation.
function bandsOf(profile){
  if(!profile)return flat();
  if(!profile.factory)return profile.bands.slice();
  return BANDS.freqs.map((_,i)=>clampBand(traceDb(profile.trace[Math.round(i*(profile.trace.length-1)/(BANDS.freqs.length-1))],profile)));
}

// ---- Library edits (local only; nothing here touches the speaker) -----------------------
const NAME_MAX=32;   // the name field's maxlength
// A free name: the base itself, or the base numbered ("Desk copy", "Desk copy 2", …), within NAME_MAX.
function uniqueName(library,base){
  const names=new Set(library.profiles.map(p=>p.name)),fit=(b,tail)=>b.slice(0,NAME_MAX-tail.length).trimEnd()+tail;
  if(!names.has(fit(base,'')))return fit(base,'');
  for(let n=2;;n++){const name=fit(base,` ${n}`);if(!names.has(name))return name;}
}
// A copy is named after the original, never after another copy ("Desk copy copy" was the bug).
const copyBase=name=>`${name.replace(/ copy( \d+)?$/,'')} copy`;
// New profiles go right after the one they were made from (or `after`), otherwise at the end of the
// user's own profiles, ahead of the factory presets.
function createProfile(library,{from=null,name,after=null}={}){
  const source=from?findProfile(library,from):null;
  const profile={key:newKey(),name:uniqueName(library,name||(source?(source.factory?`My ${source.name}`:copyBase(source.name)):'Untitled')),
    bands:source?bandsOf(source):flat(),rev:1,basedOn:source?source.key:null,approx:!!(source&&(source.factory||source.approx))};
  const order=orderOf(library),at=order.indexOf(after||(source?source.key:null));
  const next={...library,profiles:[...library.profiles,profile],order};
  if(at>=0){order.splice(at+1,0,profile.key);return {library:next,key:profile.key};}
  return {library:{...next,order:orderOf({...next,order})},key:profile.key};
}
function updateProfile(library,key,change){
  return {...library,profiles:library.profiles.map(p=>{
    if(p.key!==key)return p;
    const next={...p,...change};
    if(change.bands){next.bands=change.bands.map(clampBand);if(!sameBands(next.bands,p.bands))next.rev=p.rev+1;}
    return next;
  })};
}
const setBand=(library,key,index,value)=>{const p=findProfile(library,key);if(!p||p.factory)return library;
  const bands=p.bands.slice();bands[index]=clampBand(value);return updateProfile(library,key,{bands});};
const BANK=12,WIDGET=6;
const bankProfiles=library=>programs(library).slice(0,BANK);
// Programs keep the order the user arranges; their numbers (01, 02, …) follow it.
function moveProfile(library,key,to){
  const order=orderOf(library),from=order.indexOf(key);if(from<0)return library;
  order.splice(from,1);to=Math.max(0,Math.min(order.length,to));if(to===from)return library;
  order.splice(to,0,key);return {...library,order};
}
// An unsaved draft is kept with its profile, so it survives switching profiles and restarts.
// A draft equal to the saved curve is no draft. Saving the bands clears it.
const setDraft=(library,key,bands)=>{const p=findProfile(library,key);if(!p||p.factory)return library;
  return updateProfile(library,key,{draft:bands&&!sameBands(bands,p.bands)?bands.map(clampBand):undefined});};
const resetProfile=(library,key)=>updateProfile(library,key,{bands:flat()});
const renameProfile=(library,key,name)=>{name=String(name).trim().slice(0,32);return name?updateProfile(library,key,{name}):library;};
// Deleting keeps the record so the UI can offer undo; links to it are cleared. Factory presets are
// fixed in the speaker and cannot be deleted (they can be moved out of the way).
function deleteProfile(library,key){
  const index=library.profiles.findIndex(p=>p.key===key);if(index<0)return {library,undo:null};
  const removed=library.profiles[index],order=orderOf(library);
  const links={custom:library.links.custom?.key===key?null:library.links.custom,slots:library.links.slots.map(s=>s?.key===key?null:s)};
  return {library:{...library,profiles:library.profiles.filter(p=>p.key!==key),order:order.filter(k=>k!==key),links},undo:{profile:removed,index,links:library.links,order}};
}
function undoDelete(library,undo){
  if(!undo)return library;
  const profiles=library.profiles.slice();profiles.splice(undo.index,0,undo.profile);
  return {...library,profiles,links:undo.links,order:undo.order||library.order};
}

// ---- Speaker status ----------------------------------------------------------------------
// What the speaker holds right now, as a profile key when we can tell.
// links remember which local profile was last written to the custom slot and to each button
// slot: the speaker stores only bytes, never names.
function activeKey(speaker,library){
  if(!speaker||speaker.preset==null||speaker.preset<0)return null;
  if(speaker.preset!==CUSTOM)return 'f'+speaker.preset;
  return matchProfile(library,speaker.bands)||'custom';
}
// The saved profile whose bands are exactly these, preferring the one last written.
function matchProfile(library,bands,prefer){
  if(prefer&&sameBands(findProfile(library,prefer)?.bands,bands))return prefer;
  const link=library.links.custom?.key;
  if(link&&sameBands(findProfile(library,link)?.bands,bands))return link;
  return library.profiles.find(p=>sameBands(p.bands,bands))?.key||null;
}
// What the speaker is doing with a profile's saved version, judged by what it plays now:
//   playing – it plays exactly the saved bands
//   older   – it plays bands last written from this profile, which has been saved differently since
//   idle    – it plays something else
function profileStatus(profile,speaker,library){
  if(!profile)return 'none';
  if(!speaker||speaker.preset==null)return 'idle';
  if(profile.factory)return speaker.preset===profile.id?'playing':'idle';
  if(speaker.preset!==CUSTOM)return 'idle';
  if(sameBands(speaker.bands,profile.bands))return 'playing';
  const link=library.links.custom;
  return link&&link.key===profile.key&&sameBands(link.bands,speaker.bands)?'older':'idle';
}
// Each light: which profile it holds and whether that still matches.
//   synced  – holds the profile's saved bands (or a factory preset)
//   stale   – holds what this profile used to be; it has been saved differently since
//   changed – holds a curve we did not write (another app changed it)
function slotView(speaker,library,index){
  const hw=speaker?.buttons?.[index]??null;
  const link=library.links.slots[index];
  if(!hw||hw.preset==null||hw.preset<0)return {key:link?.key??null,status:'unknown'};
  if(hw.preset!==CUSTOM)return {key:'f'+hw.preset,status:'synced'};
  const match=matchProfile(library,hw.bands,link?.key);
  if(match)return {key:match,status:'synced'};
  if(link&&sameBands(link.bands,hw.bands)&&findProfile(library,link.key))return {key:link.key,status:'stale'};
  return {key:'custom',status:'changed'};
}

// ---- Commands sent to the speaker ---------------------------------------------------------
// Apply plays a profile now. Factory presets use their id; custom profiles fill the speaker's
// single custom slot.
function applyCommand(profile){
  return profile.factory?{type:'apply',preset:profile.id}:{type:'apply',preset:CUSTOM,bands:profile.bands.slice()};
}
// Assign writes all three button slots at once (the speaker takes them together). Each light
// holds either a factory preset or its own custom curve, independent of the applied one.
function assignCommand(keys,library){
  return {type:'assign',slots:keys.map(key=>{
    const p=findProfile(library,key);
    if(!p)throw new Error('Choose a profile for every slot.');
    return p.factory?{preset:p.id}:{preset:CUSTOM,bands:p.bands.slice()};
  })};
}
// After the speaker confirms, remember which local profiles those bytes came from.
function linkApplied(library,profile){
  if(profile.factory)return library;
  return {...library,links:{...library.links,custom:{key:profile.key,bands:profile.bands.slice()}}};
}
function linkAssigned(library,keys){
  return {...library,links:{...library.links,slots:keys.map(key=>{
    const p=findProfile(library,key);return p&&!p.factory?{key:p.key,bands:p.bands.slice()}:null;})}};
}

// ---- Display curve -------------------------------------------------------------------------
// One response sampled across log frequency, in dB, for drawing.
function sampleCurve(profile,count=160,lo=60,hi=16000){
  const LO=Math.log(lo),HI=Math.log(hi);
  if(profile?.factory){
    // The trace spans the lowest to the highest band; hold the ends outside it.
    const t=profile.trace,f0=Math.log(BANDS.freqs[0]),f1=Math.log(BANDS.freqs.at(-1));
    return Array.from({length:count},(_,i)=>{const f=LO+(HI-LO)*i/(count-1),x=Math.max(0,Math.min(1,(f-f0)/(f1-f0)))*(t.length-1);
      const j=Math.min(Math.floor(x),t.length-2),u=x-j;return traceDb(t[j]*(1-u)+t[j+1]*u,profile);});
  }
  const b=profile?profile.bands:flat(),xs=BANDS.freqs.map(f=>(Math.log(f)-LO)/(HI-LO));
  return Array.from({length:count},(_,i)=>{const x=i/(count-1);
    if(x<=xs[0])return b[0];if(x>=xs.at(-1))return b.at(-1);
    let k=xs.findIndex(v=>v>x)-1;const u=(x-xs[k])/(xs[k+1]-xs[k]);
    const p0=b[Math.max(k-1,0)],p1=b[k],p2=b[k+1],p3=b[Math.min(k+2,b.length-1)];
    return .5*(2*p1+(-p0+p2)*u+(2*p0-5*p1+4*p2-p3)*u*u+(-p0+3*p1-3*p2+p3)*u*u*u);});
}

// The widget's six buttons, and the tray menu's, are the first six programs. The host gets them
// resolved (factory buttons keep the widget's own preset names, own profiles carry name and curve).
const widgetKeys=library=>orderOf(library).slice(0,WIDGET);
const widgetSlots=library=>widgetKeys(library).map(key=>{const p=findProfile(library,key);return p.factory?{preset:p.id}:{preset:CUSTOM,name:p.name,bands:p.bands.slice()};});

// ---- the Speakers page --------------------------------------------------------------------------
// Paired Tribit speakers in rack order: the remembered one, then others the app supports, then the rest.
function rackOrder(list,address){
  const rank=d=>d.address===address?0:d.supported?1:2;
  return (list||[]).map((d,i)=>[d,i]).sort((a,b)=>rank(a[0])-rank(b[0])||a[1]-b[1]).map(x=>x[0]);
}
// A drop shorter than this (a glitch, or the host's own quick recheck) keeps the instrument.
const RACK_GRACE=3000;
// Why the page should be showing, or null. It stays out of the way unless the app cannot know which
// speaker to use ('choose') or the speaker has been gone past the grace period ('down'). The app's own
// idle release only happens while the window is hidden, and a hidden window never shows the page.
// At launch (`launch`) there is no grace: the first attempt has already settled, so a speaker that is
// not linked goes straight to the page instead of showing the instrument first. While `booting` (the
// first attempt has not finished) nothing is decided.
function rackReason({linked,status,hidden,downFor=0,list,chosen,booting=false,launch=false}){
  if(booting||hidden)return null;
  if(list&&!chosen&&list.filter(d=>d.supported).length>=2)return 'choose';
  if(linked||status==='connecting')return null;
  return launch||downFor>=RACK_GRACE?'down':null;
}
// What to say when there is no link, and the one key that can help. Try again only helps when Windows
// has the speaker but its controls are held elsewhere; a speaker that is off needs turning on, which
// no button here can do ("Reconnect" was misleading: the app never reconnects Bluetooth itself).
function dropReason(reason,error){
  switch(reason){
    case 'busy':return {word:'Controls busy',hint:'It takes one controller at a time. Close the Tribit app on your phone, then try again.',key:'retry'};
    case 'bluetooth':return {word:'Bluetooth unavailable',hint:error||'Turn on Bluetooth in Windows settings.',key:'bt'};
    case 'off':return {word:'Off or out of range',hint:'Turn it on. Windows connects it, and the app opens by itself.',key:'bt'};
    default:return {word:'Not connected',hint:'',key:null};
  }
}

// ---- loading ------------------------------------------------------------------------------------
// A library read from disk, made safe to use: profiles.json may have been edited by hand, cut short,
// or written by an older version. Anything that is not a usable profile is dropped; everything the
// views index into (bands, links.slots, widget keys) has the shape they expect. null when there is
// nothing to load. Unknown fields are kept, so a newer version's data survives an older one.
function normalizeLibrary(raw){
  if(!raw||typeof raw!=='object'||Array.isArray(raw))return null;
  const N=BANDS.freqs.length,bandsOk=b=>Array.isArray(b)&&b.length===N&&b.every(v=>Number.isFinite(v));
  const seen=new Set(),profiles=[];
  for(const p of Array.isArray(raw.profiles)?raw.profiles:[]){
    if(!p||typeof p!=='object'||typeof p.key!=='string'||!p.key||/^f\d+$/.test(p.key)||p.key==='custom'||seen.has(p.key)||!bandsOk(p.bands))continue;
    seen.add(p.key);
    const {pinned,...rest}=p;   // pinning was retired for the bank
    const next={...rest,name:(typeof p.name==='string'&&p.name.trim()?p.name.trim():'Untitled').slice(0,NAME_MAX),bands:p.bands.map(clampBand),rev:Number.isInteger(p.rev)&&p.rev>0?p.rev:1};
    delete next.draft;   // the editor is live from 0.8.0: there are no unsaved drafts to keep
    profiles.push(next);
  }
  const link=l=>l&&typeof l==='object'&&seen.has(l.key)&&bandsOk(l.bands)?{key:l.key,bands:l.bands.map(clampBand)}:null;
  const slots=Array.isArray(raw.links?.slots)?raw.links.slots:[];
  const out={...raw,profiles,links:{custom:link(raw.links?.custom),slots:[0,1,2].map(i=>link(slots[i]))}};
  // Before 0.8.0 each speaker had its own widget buttons; the widget now shows the first six programs.
  delete out.widget;delete out.widgetBy;delete out.widgetSlots;delete out.widgetSlotsBy;
  out.order=orderOf(out);
  return out;
}
// "11:56 AM" today, "12 Nov, 11:56 AM" on any other day: a time alone would read as today.
function whenSeen(ms,now=Date.now(),locale){
  if(!ms)return '';
  const d=new Date(ms),n=new Date(now),time=d.toLocaleTimeString(locale||[],{hour:'numeric',minute:'2-digit'});
  if(d.getFullYear()===n.getFullYear()&&d.getMonth()===n.getMonth()&&d.getDate()===n.getDate())return time;
  return `${d.toLocaleDateString(locale||[],{day:'numeric',month:'short',...(d.getFullYear()===n.getFullYear()?{}:{year:'numeric'})})}, ${time}`;
}

const Model={normalizeLibrary,whenSeen,rackOrder,rackReason,dropReason,RACK_GRACE,BANDS,FACTORY,CUSTOM,emptyLibrary,findProfile,matchProfile,bandsOf,createProfile,updateProfile,setBand,resetProfile,renameProfile,
  deleteProfile,undoDelete,activeKey,profileStatus,slotView,applyCommand,assignCommand,linkApplied,linkAssigned,sampleCurve,sameBands,clampBand,setDraft,moveProfile,BANK,WIDGET,bankProfiles,NAME_MAX,
  programs,orderOf,numberOf,widgetKeys,widgetSlots};
if(typeof module!=='undefined'&&module.exports)module.exports=Model;else root.Model=Model;
})(typeof globalThis!=='undefined'?globalThis:this);
