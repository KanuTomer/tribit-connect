// The app store: the profile model (model.js) over the bridge, holding what the speaker reports
// and the local profile library. Views read from it and call its actions; nothing else writes.
(function(root){
'use strict';
const M=Model,subs=new Set();
let speaker=null,library=null,settings={};
const emit=()=>subs.forEach(fn=>fn());
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
function persist(){clearTimeout(soon);soon=0;Bridge.send({type:'saveLibrary',library:{...library,widgetSlots:M.widgetSlots(library)}});}
// Drafts change with every fader step; they are written once the hand stops.
function persistSoon(){clearTimeout(soon);soon=setTimeout(persist,500);}
addEventListener('pagehide',()=>{if(soon)persist();});
Bridge.on(m=>{
  if(m.type!=='state')return;
  speaker=m.speaker;settings=m.settings||settings;
  if(!library){library=m.library||seed();if(!m.library)persist();
    library={...library,profiles:library.profiles.map(({pinned,...p})=>p)};}   // pinning was retired for the five-slot bank
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
const Core={
  M,
  // Queue a raw apply ({preset} or {preset:FE,bands}) for audition or restore.
  audition(cmd){aud.want={type:'apply',...cmd,bands:cmd.bands?cmd.bands.slice():undefined};if(!aud.busy)pump();},
  get auditioning(){return aud.busy;},
  onAuditionError:null,
  get speaker(){return speaker;},get library(){return library;},get settings(){return settings;},
  ready:()=>!!speaker&&!!library,
  linked:()=>!!speaker&&speaker.connected&&speaker.status!=='connecting',
  find:key=>key==='custom'?{key:'custom',name:'Custom',factory:false,bands:speaker?.bands||[0,0,0,0,0,0,0,0,0],foreign:true}:M.findProfile(library,key),
  factory:M.FACTORY,
  custom:()=>library.profiles,
  number:key=>{const f=M.FACTORY.findIndex(p=>p.key===key);if(f>=0)return f+1;const c=library.profiles.findIndex(p=>p.key===key);return c>=0?7+c:null;},
  active:()=>M.activeKey(speaker,library),
  slots:()=>[0,1,2].map(i=>M.slotView(speaker,library,i)),
  // The light whose content is playing now (the physical light follows the button presses).
  lit(){if(!Core.linked()||speaker.preset==null)return -1;
    return (speaker.buttons||[]).findIndex(b=>b&&b.preset===speaker.preset&&(b.preset!==M.CUSTOM||M.sameBands(b.bands,speaker.bands)));},
  status:p=>M.profileStatus(p,speaker,library),
  curve:(p,n)=>M.sampleCurve(p,n),
  bandsOf:p=>M.bandsOf(p),
  busy:null,
  async apply(p,bands){
    const target=bands?{...p,bands}:p;
    while(aud.busy)await new Promise(r=>setTimeout(r,30)); // never overtaken by an audition write
    Core.busy='apply';emit();
    const r=await Bridge.request(M.applyCommand(target));
    Core.busy=null;if(r.ok&&!target.factory){library=M.linkApplied(library,target);persist();}
    emit();return r;
  },
  // Writes all three lights, replacing one; lights we cannot name keep the speaker's own bytes.
  async assign(light,key){
    const p=M.findProfile(library,key);if(!p)return {ok:false,error:'Unknown profile.'};
    const slots=[0,1,2].map(i=>{
      if(i===light)return p.factory?{preset:p.id}:{preset:M.CUSTOM,bands:p.bands.slice()};
      const hw=speaker.buttons[i];return hw.preset===M.CUSTOM?{preset:M.CUSTOM,bands:hw.bands.slice()}:{preset:hw.preset};
    });
    Core.busy='assign:'+light;emit();
    const r=await Bridge.request({type:'assign',slots});
    Core.busy=null;
    if(r.ok){const keys=Core.slots().map(s=>s.key);keys[light]=key;
      library={...library,links:{...library.links,slots:library.links.slots.map((l,i)=>i===light?(p.factory?null:{key,bands:p.bands.slice()}):l)}};persist();}
    emit();return r;
  },
  save(key,bands){library=M.updateProfile(library,key,{bands,draft:undefined});persist();emit();},
  setDraft(key,bands){library=M.setDraft(library,key,bands);persistSoon();},
  rename(key,name){library=M.renameProfile(library,key,name);persist();emit();},
  create(opts){const r=M.createProfile(library,opts);library=r.library;persist();emit();return r.key;},
  remove(key){const r=M.deleteProfile(library,key);library=r.library;persist();emit();return r.undo;},
  undo(u){library=M.undoDelete(library,u);persist();emit();},
  move(key,to){const next=M.moveProfile(library,key,to);if(next===library)return;library=next;persist();emit();},
  // Settings > Remove my data: the page starts over with an empty library; the host deletes the files.
  removeData(){library=M.emptyLibrary();try{localStorage.clear();}catch{}Bridge.send({type:'removeData'});emit();},
  widget:()=>M.widgetKeys(library),
  setWidget(i,key){library=M.setWidget(library,i,key);persist();emit();},
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
let shift=0;
Core.transition=fn=>{
  const app=document.getElementById('app');
  if(Core.reduce()||document.hidden){fn();return;}
  clearTimeout(shift);app.classList.remove('arriving');app.classList.add('leaving');
  shift=setTimeout(()=>{app.classList.remove('leaving');fn();app.classList.add('arriving');
    shift=setTimeout(()=>app.classList.remove('arriving'),700);},280);   // long enough for the exits
};
Bridge.send({type:'ready'});
root.Store=Core;
})(this);
