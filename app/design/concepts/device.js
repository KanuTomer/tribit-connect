// Device settings sheet shared by the concept prototypes (styled per concept through CSS).
(function(root){
'use strict';
const PROMPTS=['Power on / off','Bluetooth pairing','Low battery','Maximum volume','TWS pairing'];
function render(el){
  const s=Core.speaker||{},st=Core.settings||{},can=Core.linked();
  const sw=(on,label,act,dis)=>`<button class="dv-sw" data-dv="${act}" role="switch" aria-checked="${on}" ${dis?'disabled':''}><span>${label}</span><i></i></button>`;
  el.innerHTML=`<div class="dv-head"><b>Device</b><button class="dv-close" data-dv="close" aria-label="Close">close</button></div>
    <div class="dv-big">${s.battery>=0?s.battery:'--'}<small>%</small></div>
    <dl class="dv-kv"><dt>Firmware</dt><dd>${Core.esc(s.firmware||'not reported')}</dd><dt>Link</dt><dd>${Core.esc(s.status||'—')}</dd></dl>
    <h4>Beeps</h4>${PROMPTS.map((l,i)=>sw(s.prompts?.[i]===1,l,'p'+i,!can)).join('')}
    <h4>Power</h4>${sw(s.autoShutdown===1,'Auto shutdown','auto',!can)}
    <h4>This PC</h4>${sw(!!st.release,'Release for the phone app','release')}${sw(!!st.logging,'Diagnostic logging','logging')}
    <div class="dv-row"><button data-dv="retry">Reconnect</button><button data-dv="bt">Bluetooth settings</button></div>
    <p class="dv-note">Renaming, the shutdown countdown, power off, factory reset and firmware updates stay in the Tribit app.</p>`;
}
function handle(e,close){
  const a=e.target.closest('[data-dv]')?.dataset.dv;if(!a)return false;
  const s=Core.speaker;
  if(a==='close')close();
  else if(a[0]==='p'&&a.length===2)Core.setPrompt(+a[1],s.prompts[+a[1]]!==1);
  else if(a==='auto')Core.setAutoShutdown(s.autoShutdown!==1);
  else if(a==='release')Core.send({type:'setRelease',value:!Core.settings.release});
  else if(a==='logging')Core.send({type:'setLogging',value:!Core.settings.logging});
  else if(a==='retry')Core.send({type:'retry'});else if(a==='bt')Core.send({type:'openBluetooth'});
  return true;
}
root.DeviceSheet={render,handle};
})(this);
