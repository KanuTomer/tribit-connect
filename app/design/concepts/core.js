// Shared store for the concept prototypes: the real model (js/model.js) over the bridge and the
// simulated speaker, so every concept behaves the same and only presentation differs.
(function(root){
'use strict';
const M=Model,subs=new Set();
let speaker=null,library=null,settings={};
const emit=()=>subs.forEach(fn=>fn());
function seed(){
  let lib=M.emptyLibrary();
  for(const [name,bands] of [['Night Desk',[3,4,2,0,-1,-1,0,1,2]],['Podcast',[-3,-2,0,2,3,3,1,-1,-2]],['Garden Party',[5,4,1,-1,0,1,2,4,3]]]){
    const r=M.createProfile(lib,{name});lib=r.library;lib.profiles.at(-1).bands=bands;
  }
  return lib;
}
function persist(){Bridge.send({type:'saveLibrary',library});}
Bridge.on(m=>{
  if(m.type!=='state')return;
  speaker=m.speaker;settings=m.settings||settings;
  if(!library){library=m.library&&m.library.profiles.length?m.library:seed();persist();}
  emit();
});
const Core={
  M,
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
  lit(){if(!speaker||speaker.preset==null)return -1;
    return (speaker.buttons||[]).findIndex(b=>b&&b.preset===speaker.preset&&(b.preset!==M.CUSTOM||M.sameBands(b.bands,speaker.bands)));},
  status:p=>M.profileStatus(p,speaker,library),
  curve:(p,n)=>M.sampleCurve(p,n),
  bandsOf:p=>M.bandsOf(p),
  busy:null,
  async apply(p,bands){
    const target=bands?{...p,bands}:p;
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
  save(key,bands){library=M.updateProfile(library,key,{bands});persist();emit();},
  rename(key,name){library=M.renameProfile(library,key,name);persist();emit();},
  create(opts){const r=M.createProfile(library,opts);library=r.library;persist();emit();return r.key;},
  remove(key){const r=M.deleteProfile(library,key);library=r.library;persist();emit();return r.undo;},
  undo(u){library=M.undoDelete(library,u);persist();emit();},
  setPrompt:(i,v)=>Bridge.request({type:'setPrompt',index:i,value:v}),
  setAutoShutdown:v=>Bridge.request({type:'setAutoShutdown',value:v}),
  send:m=>Bridge.send(m),
  subscribe(fn){subs.add(fn);return ()=>subs.delete(fn);},
};
// Shared small helpers for the prototypes.
Core.hz=f=>f>=1000?(f/1000)+'k':String(f);
Core.sign=v=>v>0?'+'+v:v<0?'−'+(-v):'0';
Core.esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
Core.reduce=()=>matchMedia('(prefers-reduced-motion: reduce)').matches;
// View transitions where available; instant otherwise.
Core.transition=fn=>{if(document.startViewTransition&&!Core.reduce())document.startViewTransition(fn);else fn();};
Bridge.send({type:'ready'});
root.Core=Core;
})(this);
