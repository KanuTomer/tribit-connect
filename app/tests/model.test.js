// node --test app/tests
const test=require('node:test');
const assert=require('node:assert/strict');
const M=require('../js/model.js');

const speaker=(over={})=>({connected:true,preset:1,bands:[0,0,0,0,0,0,0,0,0],buttons:[{preset:0},{preset:1},{preset:3}],...over});

test('the Speakers page shows only when the app cannot carry on by itself',()=>{
  const one=[{address:'A',supported:true}],two=[{address:'A',supported:true},{address:'B',supported:true}];
  assert.equal(M.rackReason({linked:true,status:'connected',list:one}),null);
  // a short drop, or a reconnect in progress, keeps the instrument
  assert.equal(M.rackReason({linked:false,status:'disconnected',downFor:1000,list:one}),null);
  assert.equal(M.rackReason({linked:false,status:'connecting',downFor:9000,list:one}),null);
  assert.equal(M.rackReason({linked:false,status:'disconnected',downFor:M.RACK_GRACE,list:one}),'down');
  assert.equal(M.rackReason({linked:false,status:'unavailable',downFor:5000}),'down');
  // a hidden window (where the app releases the speaker on purpose) never shows it
  assert.equal(M.rackReason({linked:false,status:'disconnected',downFor:9000,hidden:true}),null);
  // two supported speakers and no choice yet: ask, even while one is linked
  assert.equal(M.rackReason({linked:true,status:'connected',list:two}),'choose');
  assert.equal(M.rackReason({linked:true,status:'connected',list:two,chosen:true}),null);
});

test('a library from disk is made safe before it is used',()=>{
  assert.equal(M.normalizeLibrary(null),null);
  assert.equal(M.normalizeLibrary('nope'),null);
  assert.equal(M.normalizeLibrary([]),null);
  // nothing usable inside: still a library the views can draw
  const bare=M.normalizeLibrary({profiles:'x',links:7});
  assert.deepEqual(bare.profiles,[]);assert.deepEqual(bare.links,{custom:null,slots:[null,null,null]});
  const good={key:'p1',name:'Desk',bands:[1,2,3,4,5,6,7,8,99],rev:3,draft:[0,0,0,0,0,0,0,0,0],pinned:true};
  const lib=M.normalizeLibrary({future:'kept',profiles:[good,
      {key:'p1',name:'twin',bands:good.bands},          // same key twice
      {key:'p2',name:'short',bands:[1,2,3]},            // not nine bands
      {key:'f1',name:'fake factory',bands:good.bands},   // a factory key
      {key:'p3',bands:[0,0,0,0,0,0,0,0,'x']},null,7,
      {key:'p4',name:'   ',bands:[0,0,0,0,0,0,0,0,0],draft:[0,0,0,0,0,0,0,0,0]}],
    links:{custom:{key:'gone',bands:good.bands},slots:[{key:'p1',bands:good.bands},'x']},
    widget:['p1',5],widgetBy:{A:['f5','p1'],B:'x'}});
  assert.deepEqual(lib.profiles.map(p=>p.key),['p1','p4']);
  assert.equal(lib.profiles[0].bands[8],M.BANDS.range);      // clamped
  assert.equal(lib.profiles[0].pinned,undefined);assert.equal(lib.profiles[0].rev,3);
  assert.equal(lib.profiles[0].draft,undefined);   // the editor is live: drafts are not kept
  assert.equal(lib.profiles[1].name,'Untitled');assert.equal(lib.profiles[1].draft,undefined);   // a draft equal to the saved curve is none
  assert.equal(lib.links.custom,null);assert.equal(lib.links.slots.length,3);assert.equal(lib.links.slots[0].key,'p1');assert.equal(lib.links.slots[1],null);
  // per-speaker widget buttons are gone: the widget is the first six programs
  assert.equal(lib.widget,undefined);assert.equal(lib.widgetBy,undefined);assert.equal(lib.future,'kept');
  // an order is always there: own profiles, then the six factory programs
  assert.deepEqual(lib.order.slice(0,2),['p1','p4']);assert.equal(lib.order.length,8);
  // everything the views call works on the result
  assert.equal(M.widgetSlots(lib).length,6);assert.equal(M.slotView({buttons:[]},lib,2).status,'unknown');
  // a library this app wrote comes back unchanged in substance
  let mine=M.emptyLibrary();mine=M.createProfile(mine,{name:'Desk'}).library;
  assert.deepEqual(M.normalizeLibrary(JSON.parse(JSON.stringify(mine))).profiles,mine.profiles);
});

test('last seen carries the date unless it was today',()=>{
  const now=new Date(2026,10,12,15,0).getTime();
  assert.equal(M.whenSeen(0,now),'');
  assert.doesNotMatch(M.whenSeen(new Date(2026,10,12,9,5).getTime(),now,'en-GB'),/Nov/);
  assert.match(M.whenSeen(new Date(2026,10,9,9,5).getTime(),now,'en-GB'),/^9 Nov, /);
  assert.match(M.whenSeen(new Date(2025,10,9,9,5).getTime(),now,'en-GB'),/2025/);
});

test('one list of programs: own profiles and factory presets in the order the user gives them',()=>{
  let lib=M.emptyLibrary();
  // a new library is the six factory programs
  assert.deepEqual(M.programs(lib).map(p=>p.key),M.FACTORY.map(p=>p.key));
  const a=M.createProfile(lib,{name:'A'});lib=a.library;const b=M.createProfile(lib,{name:'B'});lib=b.library;
  // new profiles go ahead of the factory programs, in the order made
  assert.deepEqual(M.programs(lib).slice(0,3).map(p=>p.name),['A','B','XBass Off']);
  assert.equal(M.numberOf(lib,a.key),1);assert.equal(M.numberOf(lib,'f0'),3);assert.equal(M.numberOf(lib,'nope'),null);
  // a factory program can be moved like any other, and the order survives a save and a load
  lib=M.moveProfile(lib,'f5',0);
  assert.deepEqual(M.programs(lib).slice(0,3).map(p=>p.name),['Jazz','A','B']);
  assert.deepEqual(M.normalizeLibrary(JSON.parse(JSON.stringify(lib))).order,lib.order);
  // a library from before 0.8.0 has no order: own profiles first, factory after
  const old={profiles:lib.profiles,links:lib.links};
  assert.deepEqual(M.normalizeLibrary(old).order.slice(0,3),[a.key,b.key,'f0']);
  // an order naming things that are gone, or twice, is repaired
  assert.deepEqual(M.orderOf({...lib,order:['zz',b.key,b.key,'f1']}).slice(0,3),[b.key,a.key,'f1']);
  assert.equal(M.orderOf({...lib,order:'x'}).length,8);
});

test('launch decides at once, with no grace and no flash of the instrument',()=>{
  const one=[{address:'A',supported:true}];
  // the first attempt has not finished: nothing is shown about the speaker yet
  assert.equal(M.rackReason({linked:false,status:'disconnected',booting:true,list:one}),null);
  // it finished and the speaker is not linked: straight to the page
  assert.equal(M.rackReason({linked:false,status:'disconnected',launch:true,list:one}),'down');
  assert.equal(M.rackReason({linked:true,status:'connected',launch:true,list:one}),null);
  assert.equal(M.rackReason({linked:false,status:'connecting',launch:true,list:one}),null);
});

test('only a busy speaker offers Try again; nothing says Reconnect',()=>{
  assert.equal(M.dropReason('busy').key,'retry');
  assert.equal(M.dropReason('off').key,'bt');
  assert.equal(M.dropReason('bluetooth','Bluetooth is off.').hint,'Bluetooth is off.');
  assert.equal(M.dropReason('released').key,null);
  for(const r of ['busy','off','bluetooth','released',''])assert.doesNotMatch(JSON.stringify(M.dropReason(r)),/Reconnect/);
});

test('the rack lists the remembered speaker first, then supported, then the rest',()=>{
  const list=[{address:'C',supported:false},{address:'B',supported:true},{address:'A',supported:true}];
  assert.deepEqual(M.rackOrder(list,'A').map(d=>d.address),['A','B','C']);
  assert.deepEqual(M.rackOrder(list,'').map(d=>d.address),['B','A','C']);
});

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
test('programs can be rearranged; links follow the profile, not its place',()=>{
  let lib=M.emptyLibrary();const keys=['A','B','C'].map(name=>{const r=M.createProfile(lib,{name});lib=r.library;return r.key;});
  lib=M.linkApplied(lib,M.findProfile(lib,keys[0]));
  lib=M.moveProfile(lib,keys[0],2);
  assert.deepEqual(M.programs(lib).slice(0,3).map(p=>p.name),['B','C','A']);
  assert.equal(lib.links.custom.key,keys[0]);
  assert.equal(M.moveProfile(lib,keys[1],0),lib);
  assert.equal(M.moveProfile(lib,'nope',0),lib);
  assert.equal(M.programs(M.moveProfile(lib,keys[1],99)).at(-1).name,'B');
});
test('the program bar is the first twelve programs and the widget the first six',()=>{
  let lib=M.emptyLibrary();const keys=[];for(const n of 'ABCDEFGH'){const r=M.createProfile(lib,{name:n});lib=r.library;keys.push(r.key);}
  assert.equal(M.BANK,12);assert.equal(M.WIDGET,6);
  assert.deepEqual(M.bankProfiles(lib).map(p=>p.name),['A','B','C','D','E','F','G','H','XBass Off','XBass','MaxSound','Rock']);
  assert.deepEqual(M.widgetKeys(lib),keys.slice(0,6));
  lib=M.moveProfile(lib,'f4',0);
  assert.equal(M.bankProfiles(lib)[0].name,'Rock');assert.equal(M.widgetKeys(lib)[0],'f4');
  // the host gets the widget's buttons resolved: a preset id, or a name and its curve
  const slots=M.widgetSlots(lib);
  assert.deepEqual(slots[0],{preset:4});
  assert.equal(slots[1].preset,M.CUSTOM);assert.equal(slots[1].name,'A');assert.equal(slots[1].bands.length,9);
  // deleting a program closes the gap: the next one moves up onto the widget
  lib=M.deleteProfile(lib,keys[0]).library;
  assert.deepEqual(M.widgetKeys(lib),['f4',...keys.slice(1,6)]);
});
test('copies are numbered after the original and placed next to it',()=>{
  let lib=M.emptyLibrary();let r=M.createProfile(lib,{name:'Desk'});lib=r.library;const desk=r.key;
  r=M.createProfile(lib,{name:'Other'});lib=r.library;
  r=M.createProfile(lib,{from:desk});lib=r.library;const copy=r.key;
  r=M.createProfile(lib,{from:copy});lib=r.library;
  r=M.createProfile(lib,{from:desk});lib=r.library;
  assert.deepEqual(M.programs(lib).slice(0,5).map(p=>p.name),['Desk','Desk copy 3','Desk copy','Desk copy 2','Other']);
  // a copy of a factory program sits right after it
  r=M.createProfile(lib,{from:'f1'});lib=r.library;
  const names=M.programs(lib).map(p=>p.name);assert.equal(names[names.indexOf('XBass')+1],'My XBass');
  r=M.createProfile(lib,{name:'x'.repeat(40)});assert.equal(M.findProfile(r.library,r.key).name.length,M.NAME_MAX);
});
test('deleting keeps the order for Undo; factory programs cannot be deleted',()=>{
  let lib=M.emptyLibrary();const keys=['A','B','C'].map(name=>{const r=M.createProfile(lib,{name});lib=r.library;return r.key;});
  lib=M.moveProfile(lib,'f0',1);   // A, XBass Off, B, C, …
  const {library:after,undo}=M.deleteProfile(lib,keys[1]);
  assert.deepEqual(M.programs(after).slice(0,3).map(p=>p.name),['A','XBass Off','C']);
  assert.deepEqual(M.programs(M.undoDelete(after,undo)).slice(0,4).map(p=>p.name),['A','XBass Off','B','C']);
  assert.equal(M.deleteProfile(lib,'f0').library,lib);
});
