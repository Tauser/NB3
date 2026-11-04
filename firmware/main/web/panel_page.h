#ifndef NB3_PANEL_PAGE_H
#define NB3_PANEL_PAGE_H

// The one page of the web panel, served from flash. It asks for the code shown
// on the robot's screen, keeps the session token in memory only, and never
// polls: it reads the robot when the page opens or when a button asks.
static const char kPanelPage[] = R"HTML(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>NoiseBot</title>
<style>
:root{color-scheme:light dark;--bg:#fff;--fg:#1c1c1e;--mut:#6b6b70;--card:#f2f2f4;--line:#d8d8dc;--acc:#0a7d8c}
@media(prefers-color-scheme:dark){:root{--bg:#121214;--fg:#f2f2f4;--mut:#9a9aa0;--card:#1d1d20;--line:#333338;--acc:#35c2d3}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px system-ui,sans-serif;padding:16px;max-width:560px;margin:auto}
h1{font-size:20px;margin:0 0 12px}h2{font-size:15px;margin:0 0 10px;color:var(--mut);font-weight:600}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
button,input[type=file]{font:inherit}button{background:var(--acc);color:#fff;border:0;border-radius:8px;padding:8px 14px;cursor:pointer}
button.ghost{background:transparent;color:var(--fg);border:1px solid var(--line)}button.del{background:#b3261e}
input[type=text]{font:inherit;padding:8px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--fg);width:9em;letter-spacing:.2em}
input[type=range]{width:100%}.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.sp{justify-content:space-between}
table{width:100%;border-collapse:collapse}td{padding:6px 4px;border-top:1px solid var(--line);word-break:break-all}
.mut{color:var(--mut);font-size:14px}#msg{min-height:1.4em;color:var(--acc)}.err{color:#b3261e!important}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;margin:0}dt{color:var(--mut)}dd{margin:0;text-align:right}
</style></head><body>
<h1>NoiseBot</h1>
<div id="msg"></div>

<div id="login" class="card">
<h2>Entrar</h2>
<p class="mut">Digite o código de 6 dígitos que está na tela do robô.</p>
<div class="row"><input id="code" type="text" inputmode="numeric" maxlength="6" autocomplete="off">
<button id="go">Entrar</button></div>
</div>

<div id="app" hidden>
<div class="card"><div class="row sp"><h2>Estado</h2><button class="ghost" id="refresh">Atualizar</button></div>
<dl id="stats"></dl></div>

<div class="card"><h2>Volume</h2>
<div class="row"><input id="vol" type="range" min="0" max="100"><span id="volv" class="mut"></span></div></div>

<div class="card"><h2>LEDs</h2>
<div class="row sp"><label><input id="lon" type="checkbox"> Ligados</label>
<label><input id="lfix" type="checkbox"> Cor fixa</label><input id="lcol" type="color" value="#ffffff"></div>
<p class="mut" style="margin:10px 0 4px">Brilho: <span id="lbv"></span></p>
<input id="lbri" type="range" min="1" max="96"></div>

<div class="card"><div class="row sp"><h2>Arquivos do cartão</h2>
<select id="dir"><option value="">/</option><option>sons</option><option>slot_a</option><option>slot_b</option><option>log</option></select></div>
<table id="files"></table>
<p class="mut">Só os sons (<b>sons/nome.ogg</b>, OGG Opus até 64 KB) podem ser enviados ou apagados.</p>
<div class="row"><input id="up" type="file" accept=".ogg"><button id="send">Enviar para sons</button></div></div>
</div>

<script>
const $=id=>document.getElementById(id);let tok='';
const msg=(t,e)=>{$('msg').textContent=t;$('msg').className=e?'err':''};
async function api(path,opt={}){
  opt.headers=Object.assign({'X-Token':tok},opt.headers||{});
  const r=await fetch(path,opt);
  if(r.status===401||r.status===410){msg('Sessão encerrada. Peça ao robô para abrir o painel de novo.',1);$('app').hidden=true;$('login').hidden=false;throw 0}
  if(!r.ok)throw new Error(await r.text()||r.status);
  return r;
}
const q=o=>Object.entries(o).map(([k,v])=>k+'='+encodeURIComponent(v)).join('&');
const up=p=>encodeURIComponent(p).replace(/%2F/g,'/');
$('go').onclick=async()=>{
  const r=await fetch('/api/login',{method:'POST',body:'code='+$('code').value});
  if(r.ok){tok=(await r.json()).token;$('login').hidden=true;$('app').hidden=false;msg('');load()}
  else if(r.status===410)msg('Painel fechado. Peça ao robô para abrir de novo.',1);
  else msg('Código errado.',1);
};
async function load(){await status();await files()}
async function status(){
  const s=await (await api('/api/status')).json();
  const rows=[['Conexão',s.connection],['Comportamento',s.behavior_state],['Energia',s.energy],['Tédio',s.boredom],
    ['Cartão',s.memory_card],['Ligado há',Math.floor(s.uptime_seconds/60)+' min'],['Último reinício',s.last_restart_reason],
    ['Memória livre',s.free_memory_kb+' KB (+'+s.free_extra_memory_kb+' KB extra)'],
    ['Cartão livre',s.card_free_mb==null?'-':s.card_free_mb+' de '+s.card_total_mb+' MB']];
  $('stats').innerHTML=rows.map(([k,v])=>'<dt>'+k+'</dt><dd>'+v+'</dd>').join('');
  $('vol').value=s.volume;$('volv').textContent=s.volume;
  const l=s.led;$('lon').checked=l.on;$('lfix').checked=l.fixed;$('lbri').value=l.brightness;$('lbv').textContent=l.brightness;
  $('lcol').value='#'+l.rgb;
}
async function files(){
  const d=$('dir').value;
  const list=await (await api('/api/files?'+q({dir:d}))).json();
  $('files').innerHTML=list.map(f=>{
    const p=(d?d+'/':'')+f.name,own=d==='sons';
    return '<tr><td>'+f.name+(f.dir?'/':'')+'</td><td class="mut">'+(f.dir?'':f.size+' B')+'</td><td>'+
      (f.dir?'':'<a href="#" data-get="'+p+'">baixar</a> ')+(own?'<a href="#" data-del="'+p+'">apagar</a>':'')+'</td></tr>'}).join('')||'<tr><td class="mut">vazio</td></tr>';
}
$('files').onclick=async e=>{
  const g=e.target.dataset.get,d=e.target.dataset.del;if(!g&&!d)return;e.preventDefault();
  try{
    if(g){const b=await (await api('/api/file?'+q({path:g}).replace(/%2F/g,'/'))).blob();
      const a=document.createElement('a');a.href=URL.createObjectURL(b);a.download=g.split('/').pop();a.click()}
    else if(confirm('Apagar '+d+'?')){await api('/api/file?'+q({path:d}).replace(/%2F/g,'/'),{method:'DELETE'});msg('Apagado.');files()}
  }catch(x){if(x)msg(x.message,1)}
};
$('dir').onchange=()=>files().catch(()=>{});
$('refresh').onclick=()=>load().catch(()=>{});
$('send').onclick=async()=>{
  const f=$('up').files[0];if(!f)return msg('Escolha um arquivo .ogg.',1);
  const name=f.name.toLowerCase().replace(/[^a-z0-9_.-]/g,'_');
  try{await api('/api/file?'+q({path:'sons/'+name}).replace(/%2F/g,'/'),{method:'PUT',body:f});msg('Enviado: sons/'+name);$('dir').value='sons';files()}
  catch(x){if(x)msg(x.message,1)}
};
let t;const later=fn=>{clearTimeout(t);t=setTimeout(()=>fn().catch(x=>x&&msg(x.message,1)),250)};
$('vol').oninput=()=>{$('volv').textContent=$('vol').value;later(()=>api('/api/volume?'+q({v:$('vol').value}),{method:'POST'}))};
const led=()=>later(()=>api('/api/led?'+q({on:+$('lon').checked,b:$('lbri').value,fixed:+$('lfix').checked,rgb:$('lcol').value.slice(1)}),{method:'POST'}));
['lon','lfix','lcol'].forEach(i=>$(i).onchange=led);$('lbri').oninput=()=>{$('lbv').textContent=$('lbri').value;led()};
</script></body></html>)HTML";

#endif  // NB3_PANEL_PAGE_H
