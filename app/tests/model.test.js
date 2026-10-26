// node --test app/tests
const test=require('node:test');
const assert=require('node:assert/strict');
const M=require('../js/model.js');

const speaker=(over={})=>({connected:true,preset:1,bands:[0,0,0,0,0,0,0,0,0],buttons:[{preset:0},{preset:1},{preset:3}],...over});

test('factory presets cannot be edited',()=>{
  const lib=M.emptyLibrary();
  assert.equal(M.setBand(lib,'f1',0,3),lib);
});

test('a profile started from a factory preset is an approximation and is independent',()=>{
  let {library,key}=M.createProfile(M.emptyLibrary(),{from:'f1'});
  const p=M.findProfile(library,key);
  assert.equal(p.name,'My XBass');assert.equal(p.approx,true);assert.equal(p.bands.length,9);
  library=M.setBand(library,key,0,5);
  assert.equal(M.findProfile(library,key).bands[0],5);
  assert.equal(M.findProfile(library,key).rev,2);
});

test('names stay unique and bands are clamped to the range',()=>{
  let lib=M.emptyLibrary();
  lib=M.createProfile(lib,{name:'Desk'}).library;
  const r=M.createProfile(lib,{name:'Desk'});
  assert.equal(M.findProfile(r.library,r.key).name,'Desk 2');
  const lib2=M.setBand(r.library,r.key,3,99);
  assert.equal(M.findProfile(lib2,r.key).bands[3],M.BANDS.range);
});

test('saving, applying and assigning are separate',()=>{
  let {library,key}=M.createProfile(M.emptyLibrary(),{name:'Desk'});
  library=M.setBand(library,key,2,4);
  const p=()=>M.findProfile(library,key);
  // Not on the speaker yet.
  assert.equal(M.profileStatus(p(),speaker(),library),'idle');
  // Applied: the speaker reports exactly these bands, and is named after the profile.
  const cmd=M.applyCommand(p());
  assert.deepEqual(cmd,{type:'apply',preset:M.CUSTOM,bands:p().bands});
  library=M.linkApplied(library,p());
  const hw=speaker({preset:M.CUSTOM,bands:p().bands.slice()});
  assert.equal(M.profileStatus(p(),hw,library),'playing');
  assert.equal(M.activeKey(hw,library),key);
  // Saved differently since: the speaker still plays the older version.
  library=M.setBand(library,key,2,5);
  assert.equal(M.profileStatus(p(),hw,library),'older');
  assert.equal(M.activeKey(hw,library),'custom');
  // Something else playing: idle, however it was used before.
  assert.equal(M.profileStatus(p(),speaker({preset:4}),library),'idle');
  // A curve that matches a saved profile is recognised even if another app wrote it.
  const other=M.createProfile(library,{name:'Twin'});library=M.updateProfile(other.library,other.key,{bands:[1,1,1,1,1,1,1,1,1]});
  assert.equal(M.activeKey(speaker({preset:M.CUSTOM,bands:[1,1,1,1,1,1,1,1,1]}),library),other.key);
});

test('button slots hold factory presets or their own custom curve',()=>{
  let {library,key}=M.createProfile(M.emptyLibrary(),{name:'Desk'});
  assert.throws(()=>M.assignCommand(['f0','nope','f3'],library),/every slot/);
  const cmd=M.assignCommand(['f0',key,'f3'],library);
  assert.deepEqual(cmd.slots.map(s=>s.preset),[0,M.CUSTOM,3]);
  library=M.linkAssigned(library,['f0',key,'f3']);
  const hw=speaker({buttons:[{preset:0},{preset:M.CUSTOM,bands:M.findProfile(library,key).bands},{preset:3}]});
  assert.deepEqual(M.slotView(hw,library,1),{key,status:'synced'});
  library=M.setBand(library,key,0,2);
  assert.deepEqual(M.slotView(hw,library,1),{key,status:'stale'});
  // A light holding a curve nobody here wrote.
  const foreign=speaker({buttons:[{preset:0},{preset:M.CUSTOM,bands:[7,7,7,7,7,7,7,7,7]},{preset:3}]});
  assert.deepEqual(M.slotView(foreign,library,1),{key:'custom',status:'changed'});
  assert.deepEqual(M.slotView(hw,library,2),{key:'f3',status:'synced'});
});

test('deleting can be undone and clears links',()=>{
  let {library,key}=M.createProfile(M.emptyLibrary(),{name:'Desk'});
  library=M.linkApplied(library,M.findProfile(library,key));
  const {library:after,undo}=M.deleteProfile(library,key);
  assert.equal(after.profiles.length,0);assert.equal(after.links.custom,null);
  const back=M.undoDelete(after,undo);
  assert.equal(back.profiles[0].key,key);assert.equal(back.links.custom.key,key);
});

test('curves sample smoothly through the bands',()=>{
  const {library,key}=M.createProfile(M.emptyLibrary(),{name:'Flat'});
  assert.ok(M.sampleCurve(M.findProfile(library,key)).every(v=>v===0));
  const f=M.sampleCurve(M.findProfile(library,'f3'));
  assert.equal(f.length,160);assert.ok(Math.max(...f)-Math.min(...f)>4);
});

test('drafts live with their profile until saved',()=>{
  let lib=M.emptyLibrary();const r=M.createProfile(lib,{name:'D'});lib=r.library;
  const saved=M.findProfile(lib,r.key).bands.slice(),draft=saved.map((v,i)=>i?v:5);
  lib=M.setDraft(lib,r.key,draft);assert.deepEqual(M.findProfile(lib,r.key).draft,draft);
  assert.equal(M.findProfile(M.setDraft(lib,r.key,saved),r.key).draft,undefined);
  assert.equal(M.setDraft(lib,'f1',draft),lib);
});
test('profiles can be rearranged; links and widget buttons follow the profile, not its place',()=>{
  let lib=M.emptyLibrary();const keys=['A','B','C'].map(name=>{const r=M.createProfile(lib,{name});lib=r.library;return r.key;});
  lib=M.setWidget(lib,0,keys[0]);
  lib=M.moveProfile(lib,keys[0],2);
  assert.deepEqual(lib.profiles.map(p=>p.name),['B','C','A']);
  assert.equal(M.widgetKeys(lib)[0],keys[0]);
  assert.equal(M.moveProfile(lib,keys[1],0),lib);
  assert.deepEqual(M.moveProfile(lib,keys[1],99).profiles.map(p=>p.name),['C','A','B']);
});
test('the bank is the first six profiles',()=>{
  let lib=M.emptyLibrary();const keys=[];for(const n of 'ABCDEFG'){const r=M.createProfile(lib,{name:n});lib=r.library;keys.push(r.key);}
  assert.deepEqual(M.bankProfiles(lib).map(p=>p.name),['A','B','C','D','E','F']);
  lib=M.moveProfile(lib,keys[6],0);assert.deepEqual(M.bankProfiles(lib).map(p=>p.name),['G','A','B','C','D','E']);
});
test('copies are numbered after the original and placed next to it',()=>{
  let lib=M.emptyLibrary();let r=M.createProfile(lib,{name:'Desk'});lib=r.library;const desk=r.key;
  r=M.createProfile(lib,{name:'Other'});lib=r.library;
  r=M.createProfile(lib,{from:desk});lib=r.library;const copy=r.key;
  r=M.createProfile(lib,{from:copy});lib=r.library;
  r=M.createProfile(lib,{from:desk});lib=r.library;
  assert.deepEqual(lib.profiles.map(p=>p.name),['Desk','Desk copy 3','Desk copy','Desk copy 2','Other']);
  r=M.createProfile(lib,{name:'x'.repeat(40)});assert.equal(M.findProfile(r.library,r.key).name.length,M.NAME_MAX);
});
test('widget buttons default to the presets and fall back when a profile goes',()=>{
  let lib=M.emptyLibrary();
  assert.deepEqual(M.widgetKeys(lib),M.WIDGET_DEFAULT);
  const r=M.createProfile(lib,{name:'Desk'});lib=M.setWidget(r.library,2,r.key);
  const slots=M.widgetSlots(lib);
  assert.deepEqual(slots[0],{preset:0});
  assert.equal(slots[2].preset,M.CUSTOM);assert.equal(slots[2].name,'Desk');assert.equal(slots[2].bands.length,9);
  lib=M.deleteProfile(lib,r.key).library;
  assert.deepEqual(M.widgetSlots(lib)[2],{preset:3});
});
