// The app store: the profile model (model.js) over the bridge, holding what the speaker reports
// and the local profile library. Views read from it and call its actions; nothing else writes.
(function(root){
'use strict';
const M=Model,subs=new Set();
let speaker=null,library=null,settings={},speakers=null,bootOver=false;
setTimeout(()=>{bootOver=true;emit();},4000);
const emit=()=>subs.forEach(fn=>{try{fn();}catch(e){Bridge.report(e);}});
// The app starts with an empty library; the browser mock gets a few profiles to work with.
function seed(){
  let lib=M.emptyLibrary();
  if(Bridge.native)return lib;
  for(const [name,bands] of [['Night Desk',[3,4,2,0,-1,-1,0,1,2]],['Podcast',[-3,-2,0,2,3,3,1,-1,-2]],['Garden Party',[5,4,1,-1,0,1,2,4,3]],
    // ?many fills the row, to check how a full bank behaves
    ...(/[?&]many/.test(location.search)?['Late Night','Vocals','Outdoor','Bass Walk','Film','Piano','Desk 2','Lo-fi','Bright']:[]).map((n,i)=>[n,[i%5-2,1,0,-1,i%3,1,2,-i%4,1]])]){
    const r=M.createProfile(lib,{name});lib=r.library;lib.profiles.at(-1).bands=bands;
  }
  return lib;
}
// The host also reads the widget's buttons from here, resolved, so it never needs the model.
let soon=0;
// widgetSlots: the first six programs, resolved, for the widget and the tray menu.
function persist(){clearTimeout(soon);soon=0;
  Bridge.send({type:'saveLibrary',library:{...library,widgetSlots:M.widgetSlots(library)}});}
// Curves change with every fader step; they are written once the hand stops.
function persistSoon(){clearTimeout(soon);soon=setTimeout(persist,500);}
addEventListener('pagehide',()=>{if(soon)persist();});
Bridge.on(m=>{
  // Paired Tribit speakers (Speakers page), with the address the host remembers.
  if(m.type==='speakers'){speakers={list:m.list||[],address:m.address||'',chosen:!!m.chosen};emit();return;}
  if(m.type!=='state')return;
  speaker=m.speaker;settings=m.settings||settings;
  // The library comes once, with the host's reply to `ready` (the only state message that carries the
  // key). null means there is no file yet: start a new one. With libraryError the file exists but
  // could not be read: the host has set it aside (profiles.bad-…), and nothing is written here until
  // the user makes a change, so a file that is only locked for a moment is never replaced.
  if(!library&&'library' in m){
    const fresh=!m.library&&!m.libraryError;
    library=M.normalizeLibrary(m.library)||(m.libraryError?M.emptyLibrary():seed());
    Core.libraryError=!!m.libraryError;
    if(fresh)persist();
  }
  emit();
});
// Audition writes: unsaved curves go to the speaker one at a time and only the newest waiting
// one is sent. They are never linked to a profile. Restoring the previous sound goes through
// the same queue, so an older write can never land after it.
const aud={busy:false,want:null};
async function pump(){
  aud.busy=true;emit();
  while(aud.want){
    const cmd=aud.want;aud.want=null;
    const r=await Bridge.request(cmd);
    if(!r.ok){aud.want=null;Core.onAuditionError?.(r.error);break;}
  }
  aud.busy=false;emit();
}
// One speaker write at a time (apply, assign); a failure never blocks the ones behind it.
let chain=Promise.resolve();
const queue=job=>{const run=chain.then(job,job);chain=run.catch(()=>{});return run;};
const Core={
  M,
  // Queue a raw apply ({preset} or {preset:FE,bands}) for audition or restore.
  audition(cmd){aud.want={type:'apply',...cmd,bands:cmd.bands?cmd.bands.slice():undefined};if(!aud.busy)pump();},
  get auditioning(){return aud.busy;},
  onAuditionError:null,
  get speaker(){return speaker;},get library(){return library;},get settings(){return settings;},get speakers(){return speakers;},
  listSpeakers:()=>Bridge.send({type:'listSpeakers'}),
  // Kept by the host (state.json), so two paired speakers are asked about once, not every launch.
  get chosen(){return !!(settings.chosen||speakers?.chosen);},
  // connect=false only records the choice (the speaker already in use).
  choose(address,connect=true){settings={...settings,chosen:true};if(speakers)speakers={...speakers,chosen:true};
    if(connect){if(speakers)speakers={...speakers,address};Bridge.send({type:'chooseSpeaker',address});}else Bridge.send({type:'markChosen'});emit();},
  // The first connect attempt after start has finished (the page decides what to show only then).
  // A host that never says so is not waited on for more than a few seconds.
  get settled(){return !!speaker&&(speaker.settled!==false||bootOver);},
  get bootOver(){return bootOver;},
  // Updates (host: updates.hpp).
  checkUpdate:()=>Bridge.send({type:'checkUpdate'}),
  installUpdate:()=>Bridge.send({type:'installUpdate'}),
  setCheckUpdates:v=>{settings={...settings,checkUpdates:v};Bridge.send({type:'setCheckUpdates',value:v});emit();},
  setTray:v=>{settings={...settings,tray:v};Bridge.send({type:'setTray',value:v});emit();},
  // Windows owns the startup task and may refuse; the switch shows what the host reports back.
  setStartup:v=>Bridge.send({type:'setStartup',value:v}),
  ready:()=>!!speaker&&!!library,
  linked:()=>!!speaker&&speaker.connected&&speaker.status!=='connecting',
  find:key=>key==='custom'?{key:'custom',name:'Custom',factory:false,bands:speaker?.bands||[0,0,0,0,0,0,0,0,0],foreign:true}:M.findProfile(library,key),
  factory:M.FACTORY,
  custom:()=>library.profiles,
  // Every program in the user's order: the first twelve are the program bar, the first six the widget.
  programs:()=>M.programs(library),
  number:key=>M.numberOf(library,key),
  active:()=>M.activeKey(speaker,library),
  slots:()=>[0,1,2].map(i=>M.slotView(speaker,library,i)),
  // The light whose content is playing now (the physical light follows the button presses).
  lit(){if(!Core.linked()||speaker.preset==null)return -1;
    return (speaker.buttons||[]).findIndex(b=>b&&b.preset===speaker.preset&&(b.preset!==M.CUSTOM||M.sameBands(b.bands,speaker.bands)));},
  status:p=>M.profileStatus(p,speaker,library),
  curve:(p,n)=>M.sampleCurve(p,n),
  bandsOf:p=>M.bandsOf(p),
  busy:null,
  libraryError:false,
  // Speaker writes run one at a time, in the order asked. An assign builds its three lights from what
  // the speaker reports, so it must see the result of the write before it, not the state before that.
  apply(p,bands){return queue(async()=>{
    const target=bands?{...p,bands}:p;
    for(let n=0;aud.busy&&n<600;n++)await new Promise(r=>setTimeout(r,30)); // never overtaken by an audition write
    Core.busy='apply';emit();
    const r=await Bridge.request(M.applyCommand(target));
    Core.busy=null;if(r.ok&&!target.factory){library=M.linkApplied(library,target);persist();}
    emit();return r;
  });},
  // Writes all three lights, replacing one; lights we cannot name keep the speaker's own bytes.
  assign(light,key){return queue(async()=>{
    const p=M.findProfile(library,key);if(!p)return {ok:false,error:'Unknown profile.'};
    if(!Core.linked())return {ok:false,error:'The speaker is not connected.'};
    // The other two lights are written back as the speaker reported them; one not read yet cannot be.
    const hw=speaker.buttons||[];
    if([0,1,2].some(i=>i!==light&&(!hw[i]||hw[i].preset==null||hw[i].preset<0||(hw[i].preset===M.CUSTOM&&!Array.isArray(hw[i].bands)))))
      return {ok:false,error:'The lights have not been read yet. Try again in a moment.'};
    const slots=[0,1,2].map(i=>{
      if(i===light)return p.factory?{preset:p.id}:{preset:M.CUSTOM,bands:p.bands.slice()};
      return hw[i].preset===M.CUSTOM?{preset:M.CUSTOM,bands:hw[i].bands.slice()}:{preset:hw[i].preset};
    });
    Core.busy='assign:'+light;emit();
    const r=await Bridge.request({type:'assign',slots});
    Core.busy=null;
    if(r.ok){library={...library,links:{...library.links,slots:library.links.slots.map((l,i)=>i===light?(p.factory?null:{key,bands:p.bands.slice()}):l)}};persist();}
    emit();return r;
  });},
  save(key,bands){library=M.updateProfile(library,key,{bands,draft:undefined});persist();emit();},
  // The editor is live: a fader move is the profile's new curve at once. Views redraw what they show
  // themselves (no emit here: this runs on every step of a drag).
  setBands(key,bands){library=M.updateProfile(library,key,{bands});persistSoon();},
  rename(key,name){library=M.renameProfile(library,key,name);persist();emit();},
  create(opts){const r=M.createProfile(library,opts);library=r.library;persist();emit();return r.key;},
  remove(key){const r=M.deleteProfile(library,key);library=r.library;persist();emit();return r.undo;},
  undo(u){library=M.undoDelete(library,u);persist();emit();},
  move(key,to){const next=M.moveProfile(library,key,to);if(next===library)return;library=next;persist();emit();},
  // Settings > Remove my data: the page starts over with an empty library; the host deletes the files.
  removeData(){library=M.emptyLibrary();try{localStorage.clear();}catch{}Bridge.send({type:'removeData'});emit();},
  setPrompt:(i,v)=>Bridge.request({type:'setPrompt',index:i,value:v}),
  setAutoShutdown:v=>Bridge.request({type:'setAutoShutdown',value:v}),
  send:m=>Bridge.send(m),
  subscribe(fn){subs.add(fn);return ()=>subs.delete(fn);},
};
// Small formatting helpers shared by the views.
Core.hz=f=>f>=1000?(f/1000)+'k':String(f);
Core.sign=v=>v>0?'+'+v:v<0?'−'+(-v):'0';
Core.esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
Core.reduce=()=>matchMedia('(prefers-reduced-motion: reduce)').matches;
// Mode changes: what is shown steps back for a moment, the change happens, and the new parts
// rise into place (styles under .leaving / .arriving). Timers, not frames, drive it, so it can
// never stall in a hidden window; with reduced motion the change is instant.
// A change is never dropped: if another transition starts while one is still stepping back, the
// waiting change happens first (it used to be cancelled, which could leave two pages half shown).
let shift=0,waiting=null;
// The motion tokens in app.css (--t-norm, --t-slow, --stagger), in ms, so timers and styles never disagree.
// Exits take --t-norm plus a little stagger; arrivals take --t-slow plus the longest stagger (the faders).
Core.motion=name=>{const v=getComputedStyle(document.documentElement).getPropertyValue('--'+name).trim();return v.endsWith('ms')?parseFloat(v):v.endsWith('s')?parseFloat(v)*1000:0;};
Core.exitMs=()=>Core.motion('t-norm')+Core.motion('stagger')*3;
Core.enterMs=()=>Core.motion('t-slow')+Core.motion('stagger')*14;
Core.transition=fn=>{
  const app=document.getElementById('app');
  clearTimeout(shift);
  if(waiting){const w=waiting;waiting=null;app.classList.remove('leaving');w();}
  if(Core.reduce()||document.hidden){app.classList.remove('leaving','arriving');fn();return;}
  app.classList.remove('arriving');app.classList.add('leaving');waiting=fn;
  shift=setTimeout(()=>{waiting=null;app.classList.remove('leaving');fn();app.classList.add('arriving');
    shift=setTimeout(()=>app.classList.remove('arriving'),Core.enterMs());},Core.exitMs());
};
Bridge.send({type:'ready'});
root.Store=Core;
})(this);
