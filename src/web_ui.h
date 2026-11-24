// The settings page served at http://clock.local/ (one self-contained page).
#pragma once

static const char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Clock</title>
<style>
:root{color-scheme:dark}
body{font:15px/1.4 system-ui,sans-serif;background:#111;color:#ddd;margin:0 auto;padding:16px;max-width:520px}
h1{font-size:19px;margin:0 0 2px}
#st{color:#9a9a9a;font-size:13px;margin-bottom:14px;min-height:5.6em}
#st b{color:#ddd;font-weight:600}
fieldset{border:1px solid #333;border-radius:12px;margin:0 0 14px;padding:8px 14px 12px}
legend{color:#ffb35c;padding:0 6px;font-weight:600}
.row{display:flex;align-items:center;gap:10px;margin:10px 0}
.row span:first-child{min-width:5.2em;color:#bbb}
input[type=range]{flex:1;accent-color:#ffb35c}
.v{min-width:2.4em;text-align:right;color:#fff;font-variant-numeric:tabular-nums}
.seg{display:flex;gap:6px;flex-wrap:wrap;margin:8px 0}
.seg button{flex:1;min-width:5em;padding:9px 6px;border-radius:9px;border:1px solid #444;background:#1c1c1c;color:#ddd;font:inherit;cursor:pointer}
.seg button.on{background:#ffb35c;color:#111;border-color:#ffb35c;font-weight:600}
.seg button:disabled{opacity:.35;cursor:default}
select{background:#1c1c1c;color:#ddd;border:1px solid #444;border-radius:7px;padding:5px 8px;font:inherit}
.note{color:#888;font-size:12.5px;margin:4px 0 0}
.hide{display:none}
a{color:#ffb35c}
</style></head><body>
<h1>Living-room clock</h1>
<div id="st">connecting…</div>

<fieldset><legend>Clock font</legend>
<div class="seg" data-k="clockFont" data-o="Segment|Rounded|Slim|Dots"></div>
</fieldset>

<fieldset><legend>Brightness</legend>
<div class="seg" data-k="brightMode" data-o="Auto (sensor)|Manual"></div>
<div class="row" id="dayRow"><span>Level</span><input type="range" min="0" max="100" data-k="dayLevel"><span class="v"></span></div>
<div id="autoBox">
<div class="row"><span>Dark room</span><input type="range" min="0" max="100" data-k="autoMin"><span class="v"></span></div>
<div class="row"><span>Bright room</span><input type="range" min="0" max="100" data-k="autoMax"><span class="v"></span></div>
</div>
<p class="note" id="noSensor">No light sensor found, so the clock uses this level all day.</p>
<div class="seg" data-k="hwBrightness" data-o="Dim pixels|Panel command"></div>
<p class="note">Dim pixels: slider 0 = 22 % pixel brightness, 100 = full. If moving the slider does nothing in “Panel command”, your panel ignores that command.</p>
</fieldset>

<fieldset><legend>Night</legend>
<div class="seg" data-k="nightMode" data-o="Off|Schedule|Light sensor"></div>
<div class="row" id="sched"><span>From</span><select data-k="nightStart"></select><span>to</span><select data-k="nightEnd"></select></div>
<div class="row"><span>Level</span><input type="range" min="0" max="100" data-k="nightLevel"><span class="v"></span></div>
<div class="seg" data-k="nightRed" data-o="Normal screen|Red clock only"></div>
</fieldset>

<fieldset id="inBox" class="hide"><legend>Indoor temperature</legend>
<div class="seg" data-k="indoorSlide" data-o="Weather only|Slide to indoor every minute"></div>
<p class="note">Daytime only: at each new minute the weather slides down to the indoor temperature for a few seconds, then back.</p>
<div class="row"><span>Calibration</span><input type="range" min="0" max="100" data-k="indoorOffset" data-f="off"><span class="v"></span></div>
<p class="note" id="inNote"></p>
</fieldset>

<p class="note">Changes apply instantly and are saved on the clock. <a href="/status.txt">Plain status</a></p>

<script>
let S={}, st={}, dragging=null, timer=null;
const q=s=>document.querySelectorAll(s);
// slider value as shown: plain number, or the indoor offset in degC (50 = 0.0)
const fmt=(r,v)=>r.dataset.f==='off'?((v>50?'+':'')+((v-50)/10).toFixed(1)+' °C'):v;

q('select').forEach(sel=>{for(let h=0;h<24;h++){const o=document.createElement('option');o.value=h;o.textContent=(h<10?'0':'')+h+':00';sel.appendChild(o);}});
q('.seg').forEach(seg=>{seg.dataset.o.split('|').forEach((label,i)=>{const b=document.createElement('button');b.textContent=label;b.onclick=()=>send(seg.dataset.k,i);seg.appendChild(b);});});
q('input[type=range]').forEach(r=>{
  r.addEventListener('pointerdown',()=>dragging=r);
  r.addEventListener('pointerup',()=>{dragging=null;});
  r.addEventListener('input',()=>{r.nextElementSibling.textContent=fmt(r,r.value);S[r.dataset.k]=+r.value;
    clearTimeout(timer);timer=setTimeout(()=>post(r.dataset.k,r.value),120);});
  r.addEventListener('change',()=>{dragging=null;post(r.dataset.k,r.value);});
});
q('select').forEach(sel=>sel.onchange=()=>send(sel.dataset.k,+sel.value));

function send(k,v){S[k]=v;draw();post(k,v);}
function post(k,v){const b=new URLSearchParams();b.set(k,v);
  fetch('/api/settings',{method:'POST',body:b}).then(r=>r.json()).then(load).catch(()=>{});}
function load(j){S=j.settings;st=j.status;draw();}

function draw(){
  const sensor=!!st.sensor, auto=S.brightMode===0&&sensor;
  q('.seg').forEach(seg=>{const k=seg.dataset.k;let v=S[k];
    if(!sensor&&k==='brightMode')v=1;            // no sensor: effectively manual
    if(!sensor&&k==='nightMode'&&v===2)v=1;      // ...and night falls back to the schedule
    [...seg.children].forEach((b,i)=>b.classList.toggle('on',i===v));});
  q('input[type=range]').forEach(r=>{if(r===dragging)return;r.value=S[r.dataset.k];r.nextElementSibling.textContent=fmt(r,S[r.dataset.k]);});
  q('select').forEach(sel=>{if(document.activeElement!==sel)sel.value=S[sel.dataset.k];});
  document.querySelector('[data-k=brightMode]').children[0].disabled=!sensor;
  document.querySelector('[data-k=nightMode]').children[2].disabled=!sensor;
  document.getElementById('autoBox').classList.toggle('hide',!auto);
  document.getElementById('dayRow').classList.toggle('hide',auto);
  document.getElementById('noSensor').classList.toggle('hide',sensor);
  document.getElementById('sched').classList.toggle('hide',S.nightMode!==1&&!(S.nightMode===2&&!sensor));
  document.getElementById('inBox').classList.toggle('hide',!st.ntc);
  document.getElementById('inNote').innerHTML=st.indoor!==undefined
    ?'Sensor reads <b>'+st.indoorRaw+' °C</b> before calibration. Put a trusted thermometer next to it for an hour, then move the slider until the clock agrees.'
    :(st.ntc?'<b style="color:#f66">Thermometer: '+(st.indoorFault||'no reading yet')+'</b>':'');
  const p=[];
  p.push('<b>'+(st.time||'--:--')+'</b>');
  if(st.temp!==undefined)p.push('<b>'+st.temp+' °C</b> out');
  if(st.indoor!==undefined)p.push('<b>'+st.indoor+' °C</b> in');
  p.push('panel '+(st.panel?'connected':'<b style="color:#f66">offline</b>'));
  p.push('brightness <b>'+st.level+'</b>'+(S.hwBrightness===0?' ('+st.pixelPct+' %)':'')+(st.night?' · night':''));
  if(sensor)p.push(st.lux+' lx');
  const ago=s=>s<120?s+' s':s<7200?Math.round(s/60)+' min':Math.round(s/3600)+' h';
  const link='link drops <b>'+(st.drops||0)+'</b>'+(st.drops?' (last '+ago(st.lastDropAgo)+' ago: '+st.lastDrop+')':'');
  const cal='calendar: '+(st.calendar||'-');
  document.getElementById('st').innerHTML=p.join(' · ')+'<br>'+link+'<br>'+cal+'<br>'+(st.ip||'')+' · up '+(st.uptime||'')+' · <a href="/update">firmware '+(st.fw||'')+'</a>';
}
function poll(){fetch('/api/state').then(r=>r.json()).then(load).catch(()=>{document.getElementById('st').textContent='clock not reachable';});}
poll();setInterval(()=>{if(!dragging)poll();},3000);
</script>
</body></html>)HTML";
