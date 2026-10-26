// The XSound Plus 2, drawn from the product: a stadium-shaped bar with a fabric front, a
// TRIBIT badge, and seven top buttons (power, Bluetooth, −, play, +, EQ, party link). The EQ
// button carries the light that tells which of its three programs is playing.
(function(root){
'use strict';
const LIGHT=['#4a4b51','#f7f8fb','#3d8bff'];
// A stadium (rounded bar) as a path.
const stad=(x,y,w,h)=>{const r=h/2;return `M${x+r} ${y}H${x+w-r}A${r} ${r} 0 0 1 ${x+w-r} ${y+h}H${x+r}A${r} ${r} 0 0 1 ${x+r} ${y}Z`;};
// Top-panel glyphs, centred on (x,y).
const glyph={
  power:(x,y)=>`<path d="M${x-4.5} ${y-3.2}A6 6 0 1 0 ${x+4.5} ${y-3.2}" /><path d="M${x} ${y-7}V${y-1}"/>`,
  bt:(x,y)=>`<path d="M${x-4} ${y-3.5}L${x+4} ${y+3.5}L${x} ${y+7}V${y-7}L${x+4} ${y-3.5}L${x-4} ${y+3.5}"/>`,
  minus:(x,y)=>`<path d="M${x-5} ${y}H${x+5}"/>`,
  play:(x,y)=>`<circle cx="${x}" cy="${y}" r="5.5"/>`,
  plus:(x,y)=>`<path d="M${x-5} ${y}H${x+5}M${x} ${y-5}V${y+5}"/>`,
  link:(x,y)=>`<path d="M${x} ${y}C${x+2.4} ${y-3.8} ${x+7.6} ${y-3.8} ${x+7.6} ${y}S${x+2.4} ${y+3.8} ${x} ${y}S${x-7.6} ${y-3.8} ${x-7.6} ${y}S${x-2.4} ${y+3.8} ${x} ${y}"/>`,
};
const BUTTONS=[['power',178],['bt',232],['minus',286],['play',326],['plus',366],['eq',424],['link',478]];

// The EQ ring takes the light's colour; the off light still reads as the active ring.
const ring=light=>light===0?'#8a8c94':light>0?LIGHT[light]:'#4a4b51';
// Changes the light and link LED of a drawn speaker in place, so they fade rather than jump.
function set(root,{light=-1,state='linked'}){
  root.querySelectorAll('.spk-halo').forEach(h=>{h.style.opacity=+h.dataset.h===light?1:0;});
  const r=root.querySelector('.spk-ring');if(r)r.style.stroke=ring(light);
  const led=root.querySelector('.spk-led');if(led){led.setAttribute('class','spk-led '+state);led.style.fill=state==='off'?'#55565c':'#3d8bff';}
}
function svg({light=-1,state='linked',style='product',id='speaker',interactive=true}={}){
  const line=style==='line';
  const ink=line?'currentColor':'#7d7e86';
  const top=BUTTONS.map(([g,x])=>{
    const y=35;
    if(g==='eq'){
      // The button is barely larger than its neighbours; its light spreads as a soft glow.
      return `<g class="spk-eq" data-eq ${interactive?'tabindex="0" role="button" aria-label="EQ button"':''}>
        ${line?'':[1,2].map(i=>`<circle cx="${x}" cy="${y}" r="24" fill="url(#${id}-halo${i})" class="spk-halo" data-h="${i}" style="opacity:${light===i?1:0}"/>`).join('')}
        <circle cx="${x}" cy="${y}" r="9.5" fill="${line?'none':'#2b2c31'}" stroke="${line?'currentColor':ring(light)}" stroke-width="${line?1.2:1.7}" class="spk-ring"/>
        <path d="M${x-3.9} ${y+1.4}v-2.8M${x-1.3} ${y+3}v-6M${x+1.3} ${y+2.2}v-4.4M${x+3.9} ${y+1}v-2" stroke="${line?'currentColor':'#9a9ba3'}" stroke-width="1.3" stroke-linecap="round"/></g>`;
    }
    const led=g==='bt'&&!line?`<circle cx="${x+13}" cy="${y-6}" r="2.2" class="spk-led ${state}" fill="${state==='off'?'#55565c':'#3d8bff'}"/>`:'';
    return `<g fill="none" stroke="${ink}" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">${glyph[g](x,y)}</g>${led}`;
  }).join('');
  if(line){
    // Service-manual style: outlines only, with a dimension line.
    return `<svg class="speaker line" viewBox="0 0 640 270" role="img" aria-label="XSound Plus 2 drawing" fill="none" stroke="currentColor" stroke-width="1.2">
      <path d="${stad(20,18,600,180)}"/><path d="M20 108V142M620 108V142"/><path d="${stad(20,52,600,180)}" fill="var(--paper,#f2efe6)"/>
      <path d="${stad(40,70,560,146)}" stroke-dasharray="2 3"/>
      <rect x="288" y="133" width="64" height="22" rx="5"/><text x="320" y="148" text-anchor="middle" font-size="9" letter-spacing="2" stroke="none" fill="currentColor">TRIBIT</text>
      ${top}
      <path d="M20 248H620M20 242v12M620 242v12" stroke-width=".8"/><text x="320" y="264" text-anchor="middle" font-size="10" stroke="none" fill="currentColor">282</text>
    </svg>`;
  }
  return `<svg class="speaker product" id="${id}" viewBox="0 0 640 240" role="img" aria-label="XSound Plus 2">
    <defs>
      <pattern id="${id}-mesh" width="5" height="5" patternUnits="userSpaceOnUse"><rect width="5" height="5" fill="#1d1e22"/><circle cx="2.5" cy="2.5" r="1.05" fill="#34353b"/></pattern>
      <linearGradient id="${id}-body" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#2e2f35"/><stop offset="1" stop-color="#1f2024"/></linearGradient>
      <linearGradient id="${id}-top" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#404147"/><stop offset="1" stop-color="#34353b"/></linearGradient>
      <linearGradient id="${id}-rim" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".14"/><stop offset=".08" stop-color="#fff" stop-opacity="0"/></linearGradient>
      ${LIGHT.map((c,i)=>`<radialGradient id="${id}-halo${i}"><stop offset="0" stop-color="${c}" stop-opacity=".5"/><stop offset=".4" stop-color="${c}" stop-opacity=".2"/><stop offset="1" stop-color="${c}" stop-opacity="0"/></radialGradient>`).join('')}
    </defs>
    <path d="M110 18H530A90 90 0 0 1 620 108V142A90 90 0 0 1 530 232H110A90 90 0 0 1 20 142V108A90 90 0 0 1 110 18Z" fill="url(#${id}-top)"/>
    <path d="${stad(20.5,18.5,599,179)}" fill="none" stroke="url(#${id}-rim)" stroke-width="1.2"/>
    <path d="${stad(20,52,600,180)}" fill="url(#${id}-body)"/>
    <path d="${stad(20.6,52.6,598.8,178.8)}" fill="none" stroke="url(#${id}-rim)" stroke-width="1.2"/>
    <path d="${stad(36,66,568,152)}" fill="url(#${id}-mesh)"/>

    <rect x="288" y="131" width="64" height="22" rx="5" fill="#141518" stroke="#4a4b51" stroke-width="1"/>
    <text x="320" y="146" text-anchor="middle" font-size="9.5" font-weight="600" letter-spacing="2.2" fill="#c9cad0" font-family="Archivo,Arial">TRIBIT</text>
    ${top}
  </svg>`;
}
root.Speaker={svg,set,LIGHT,NAMES:['Light off','White light','Blue light'],SHORT:['off','white','blue']};
})(this);
