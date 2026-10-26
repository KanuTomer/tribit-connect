// Light, dark, or whatever Windows uses. Loaded before the stylesheet paints, so the page never
// shows the other colour first; the host gets the result for its window background.
(function(root){
'use strict';
const KEY='tribit-connect.theme',system=matchMedia('(prefers-color-scheme: dark)');
let choice='device';
try{choice=localStorage.getItem(KEY)||'device';}catch{}
// A change of theme cross-fades the colours instead of snapping.
function fade(){const el=document.documentElement;el.classList.add('theming');clearTimeout(fade.t);fade.t=setTimeout(()=>el.classList.remove('theming'),450);}
function apply(){
  const dark=choice==='dark'||(choice==='device'&&system.matches);
  document.documentElement.dataset.theme=dark?'dark':'light';
  root.chrome?.webview?.postMessage({type:'theme',dark});
}
system.addEventListener('change',()=>{if(choice==='device'){fade();apply();}});
apply();
root.Theme={get choice(){return choice;},set(value){fade();choice=value;try{localStorage.setItem(KEY,value);}catch{}apply();}};
})(this);
