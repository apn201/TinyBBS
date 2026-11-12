#pragma once

static const char kWebTerminal[] PROGMEM = R"TERM(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, interactive-widget=resizes-content">
<title>Kouvosto // Terminal</title>
<style>
html,body{margin:0;background:#000;color:#aaa;font:14px monospace}
   header,footer{padding:5px;color:#5ff;display:flex;gap:6px;align-items:center;flex-wrap:wrap}
button{background:#000;color:inherit;border:1px solid #066;font:inherit;min-height:32px}
   #carrier{flex:1;min-width:18ch}
canvas{display:block;width:100%;height:auto;image-rendering:pixelated}
#screen{position:relative;width:min(100%,960px,max(160px,calc((var(--visible-height,100dvh) - 84px) * 1.6)));
touch-action:none}
#capture{position:absolute;width:1.2ch;height:1em;overflow:hidden;color:transparent;
caret-color:transparent;outline:none;white-space:pre;pointer-events:none}
#links:empty{display:none} a{color:#5ff}
#viewer{position:fixed;inset:0;background:#000;display:flex;flex-direction:column;z-index:10;cursor:pointer}
#viewer[hidden]{display:none}
#pic{flex:1;min-height:0;width:100%;object-fit:contain;image-rendering:pixelated;clip-path:inset(0 0 100% 0)}
#pic.shown{clip-path:inset(0);transition:clip-path 1.4s steps(48)}
#vbar{background:#00a;color:#fff;padding:4px 8px;white-space:pre;overflow:hidden;text-overflow:ellipsis}
</style></head><body>
 <header><button id="dial">DIAL</button> <button id="modem" type="button">MODEM AUDIO</button> <button id="speed" type="button" title="Modem speed for ANSI pictures">SPEED</button> <span id="carrier" role="status">KOUVOSTO // NO CARRIER</span></header>
<div id="screen"><canvas id="display" width="640" height="400" aria-label="80-column Kouvosto terminal"></canvas>
<div id="capture" contenteditable="plaintext-only" role="textbox" aria-label="Terminal input"
autocapitalize="off" autocorrect="off" spellcheck="false" inputmode="text"
data-lpignore="true" data-1p-ignore data-bwignore></div></div>
<footer><button id="enter" disabled>ENTER</button> <button id="back" disabled>BACKSPACE</button>
<span id="links"></span></footer>
<div id="viewer" hidden><img id="pic" alt=""><div id="vbar"></div></div>
<script>
'use strict';
const $=id=>document.getElementById(id);
 const display=$('display'),ctx=display.getContext('2d',{alpha:false}),capture=$('capture');
 const dial=$('dial'),modem=$('modem'),carrier=$('carrier'),enter=$('enter'),back=$('back'),links=$('links');
const palette=['#000','#a00','#0a0','#a50','#00a','#a0a','#0aa','#aaa',
               '#555','#f55','#5f5','#ff5','#55f','#f5f','#5ff','#fff'];
const cols=80,rows=25,w=8,h=16;
let fg=7,bg=0,bright=false,reverse=false,x=0,y=0,wrap=false,partial='',saved=[0,0];
const blank=()=>({ch:' ',ink:7,paper:0});
const row=()=>Array.from({length:cols},blank);
let screen=Array.from({length:rows},row);
let node=0,token='',connected=false,busy=false,secret=false,frame=0,failures=0;
// streamOn: key mode from the board. 1 = a picture is streaming, keys skip
// or pause it. 2 = the reader, every key goes to the board at once.
// history/viewOff: lines that scrolled off the top, and how far back we look.
let streamOn=0,history=[],viewOff=0;
let pending=null,timer=0;
 let modemPlayer=null,modemPlaying=false;
 function playModem(){
   if(modemPlaying)return;
   modemPlaying=true;modem.disabled=true;
   modemPlayer=modemPlayer||new Audio('/modem.mp3');
   modemPlayer.currentTime=0;
   modemPlayer.onended=()=>{modemPlaying=false;modem.disabled=false;carrier.textContent=connected?'KOUVOSTO // CARRIER CONNECTED':'KOUVOSTO // MODEM READY';};
   modemPlayer.onerror=()=>{modemPlaying=false;modem.disabled=false;carrier.textContent='KOUVOSTO // MODEM AUDIO UNAVAILABLE';};
   modemPlayer.play().catch(()=>{modemPlaying=false;modem.disabled=false;carrier.textContent='KOUVOSTO // AUDIO BLOCKED';});
 }
function move(nx,ny){
  x=Math.max(0,Math.min(cols-1,nx));
  y=Math.max(0,Math.min(rows-1,ny));
  wrap=false;
}
function down(){
  if(y<rows-1)y++;
  else{history.push(screen.shift());if(history.length>1000)history.shift();screen.push(row());}
}
function colors(){
  const ink=fg|(bright?8:0);
  return reverse?[bg,ink]:[ink,bg];
}
function put(ch){
  if(wrap){x=0;down();wrap=false;}
  const [ink,paper]=colors();
  screen[y][x]={ch,ink,paper};
  if(x===cols-1)wrap=true;
  else x++;
}
function clear(){
  screen=Array.from({length:rows},row);
  links.replaceChildren();
}
function sgr(args){
  for(const n of args){
    if(n===0){fg=7;bg=0;bright=reverse=false;}
    else if(n===1)bright=true;
    else if(n===22||n===2)bright=false;
    else if(n===7)reverse=true;
    else if(n===27)reverse=false;
    else if(n>=30&&n<=37)fg=n-30;
    else if(n>=90&&n<=97)fg=n-82;
    else if(n>=40&&n<=47)bg=n-40;
    else if(n>=100&&n<=107)bg=n-92;
    else if(n===39)fg=7;
    else if(n===49)bg=0;
  }
}
function eraseLine(from,to){
  const [ink,paper]=colors();
  for(let col=from;col<to;col++)screen[y][col]={ch:' ',ink,paper};
}
function control(params,cmd){
  const args=params.split(';').map(Number);
  const n=args[0]||1;
  if(cmd==='m')sgr(args);
  else if(cmd==='H'||cmd==='f')move((args[1]||1)-1,n-1);
  else if(cmd==='A')move(x,y-n);
  else if(cmd==='B')move(x,y+n);
  else if(cmd==='C')move(x+n,y);
  else if(cmd==='D')move(x-n,y);
  else if(cmd==='G')move(n-1,y);
  else if(cmd==='s')saved=[x,y];
  else if(cmd==='u')move(...saved);
  else if(cmd==='J'&&args[0]===2){clear();move(0,0);}
  else if(cmd==='K'){
    const mode=args[0];
    eraseLine(mode===0?x:0,mode===1?x+1:cols);
  }
}
// The image viewer: the board names the picture, the picture is drawn in
// top to bottom like a 90s viewer decoding it, and keys go back to the board.
const viewer=$('viewer'),pic=$('pic'),vbar=$('vbar');
function closeViewer(){if(viewer.hidden)return;viewer.hidden=true;pic.removeAttribute('src');}
function showViewer(href,name,size){
  viewer.hidden=false;pic.classList.remove('shown');void pic.offsetWidth;
  vbar.textContent=' TINYVIEW  '+name+'  '+size+'  decoding...';
  pic.onload=()=>{
    vbar.textContent=' '+name+'  '+pic.naturalWidth+'x'+pic.naturalHeight+'  '+size+'    N next  P prev  ESC exit';
    pic.classList.add('shown');
  };
  pic.onerror=()=>{vbar.textContent=' '+name+'  cannot be shown    N next  P prev  ESC exit';};
  pic.src=href;focusTerminal();
}
viewer.addEventListener('click',e=>{const f=e.clientX/innerWidth;streamKey(f<0.3?'p':f>0.7?'n':'q');focusTerminal();});
// Uploads: the board arms a one-file upload; the caller picks the file and a
// transfer meter runs below the terminal while it is sent.
let upXhr=null;
function armUpload(href,max){
  const pick=document.createElement('input');pick.type='file';pick.hidden=true;
  const btn=document.createElement('button');btn.type='button';btn.textContent='CHOOSE FILE';
  const meter=document.createElement('span');
  btn.onclick=()=>pick.click();
  pick.onchange=()=>{
    const f=pick.files[0];if(!f)return;
    if(max&&f.size>max){meter.textContent=' '+f.name+' is too large';return;}
    btn.disabled=true;
    const form=new FormData();form.append('file',f,f.name);
    const xhr=new XMLHttpRequest();upXhr=xhr;const t0=Date.now();
    xhr.upload.onprogress=e=>{
      if(!e.lengthComputable)return;
      const cps=Math.round(e.loaded/Math.max(.1,(Date.now()-t0)/1000));
      meter.textContent=' SENDING '+f.name+'  '+Math.floor(e.loaded*100/e.total)+'%  '+cps+' cps';
    };
    xhr.onload=()=>{upXhr=null;if(xhr.status===200)links.replaceChildren();else{meter.textContent=' '+(xhr.responseText||'Upload failed');btn.disabled=false;}};
    xhr.onerror=()=>{upXhr=null;meter.textContent=' Upload failed';btn.disabled=false;};
    xhr.open('POST',href);xhr.send(form);
  };
  links.replaceChildren(btn,pick,meter);
}
// Music: the player script comes from the card the first time it is needed.
let playerLoad=null;
const playerOpen=()=>!!(window.TinyPlayer&&window.TinyPlayer.isOpen());
function closePlayer(){if(window.TinyPlayer)window.TinyPlayer.close();}
function playTune(href,name,size,desc){
  if(!playerLoad)playerLoad=new Promise((ok,fail)=>{const s=document.createElement('script');s.src='/player/player.js?t='+Date.now();s.onload=ok;s.onerror=()=>{playerLoad=null;fail();};document.head.appendChild(s);});
  playerLoad.then(()=>{window.TinyPlayer.open(href,name,size,streamKey,desc);focusTerminal();}).catch(()=>{});
}
document.addEventListener('click',()=>{if(playerOpen())focusTerminal();});
function downloadLink(message){
  if(message.startsWith('9;MOD;')){
    const rest=message.slice(6);
    if(rest==='close'){closePlayer();return;}
    const parts=rest.split(';');
    try{
      const url=new URL(parts[0],location.href);
      if(url.origin!==location.origin||url.pathname!=='/music')return;
      playTune(url.href,parts[1]||'',parts[2]||'',parts.slice(3).join(';'));
    }catch{}
    return;
  }
  if(message.startsWith('9;UP;')){
    const rest=message.slice(5);
    if(rest==='close'){if(!upXhr)links.replaceChildren();return;}
    const cut=rest.indexOf(';');
    try{
      const url=new URL(cut<0?rest:rest.slice(0,cut),location.href);
      if(url.origin!==location.origin||url.pathname!=='/upload')return;
      armUpload(url.href,cut<0?0:parseInt(rest.slice(cut+1),10)||0);
    }catch{}
    return;
  }
  if(message.startsWith('9;IMG;')){
    const rest=message.slice(6);
    if(rest==='close'){closeViewer();return;}
    const parts=rest.split(';');
    try{
      const url=new URL(parts[0],location.href);
      if(url.origin!==location.origin||url.pathname!=='/view')return;
      showViewer(url.href,parts[1]||'',parts[2]||'');
    }catch{}
    return;
  }
  if(message.startsWith('9;LINK;')){
    const cut=message.indexOf(';',7);
    if(cut<0)return;
    try{
      const url=new URL(message.slice(7,cut),location.href);
      if(url.origin!==location.origin||url.pathname!=='/sysop')return;
      const link=document.createElement('a');
      link.href=url.href;link.target='_blank';link.rel='noopener';link.textContent=message.slice(cut+1);
      links.replaceChildren(link);
    }catch{}
    return;
  }
  if(!message.startsWith('9;DL;'))return;
  const cut=message.indexOf(';',5);
  if(cut<0)return;
  try{
    const url=new URL(message.slice(5,cut),location.href);
    if(url.origin!==location.origin||url.pathname!=='/download')return;
    const link=document.createElement('a');
    link.href=url.href;link.download='';link.textContent=message.slice(cut+1);
    links.replaceChildren(link);
  }catch{}
}
function render(data){
  data=partial+data;partial='';
  for(let i=0;i<data.length;){
    const start=i,ch=data[i++];
    if(ch==='\x1b'){
      if(i===data.length){partial=data.slice(start);break;}
      const kind=data[i++];
      if(kind==='['){
        const p=i;
        while(i<data.length&&data[i]!=='\x1b'&&!/[@-~]/.test(data[i]))i++;
        if(i===data.length){partial=data.slice(start);break;}
        // A new ESC inside the parameters means the sequence was cut: drop it
        // and start over, instead of swallowing the next sequence.
        if(data[i]==='\x1b')continue;
        control(data.slice(p,i),data[i++]);
      }else if(kind===']'){
        const end=data.indexOf('\x07',i);
        if(end<0){partial=data.slice(start);break;}
        downloadLink(data.slice(i,end));i=end+1;
      }
    }else if(ch==='\r')move(0,y);
    else if(ch==='\n'){wrap=false;down();}
    else if(ch==='\b')move(x-1,y);
    else if(ch==='\t')move((x+8)&~7,y);
    else if(ch>=' '&&ch!=='\x7f')put(ch);
  }
  if(partial.length>1024)partial='';
  repaint();
}
function drawCell(cell,col,line){
  const px=col*w,py=line*h;
  ctx.fillStyle=palette[cell.paper];ctx.fillRect(px,py,w,h);
  ctx.fillStyle=palette[cell.ink];
  // Block and shade glyphs are drawn, not typed: fonts render them unevenly.
  const shade=cell.ch==='░'?.25:cell.ch==='▒'?.5:cell.ch==='▓'?.75:0;
  if(shade){ctx.globalAlpha=shade;ctx.fillRect(px,py,w,h);ctx.globalAlpha=1;}
  else if(cell.ch==='█')ctx.fillRect(px,py,w,h);
  else if(cell.ch==='▀')ctx.fillRect(px,py,w,h/2);
  else if(cell.ch==='▄')ctx.fillRect(px,py+h/2,w,h/2);
  else if(cell.ch==='▌')ctx.fillRect(px,py,w/2,h);
  else if(cell.ch==='▐')ctx.fillRect(px+w/2,py,w/2,h);
  else if(cell.ch===' ' )return;
  else ctx.fillText(cell.ch,px,py+13,w);
}
function repaint(){
  saveScreen();
  if(frame)return;
  frame=requestAnimationFrame(()=>{
    frame=0;
    ctx.font='14px "Courier New", monospace';
    viewOff=Math.min(viewOff,history.length);
    const shown=viewOff?history.slice(history.length-viewOff).concat(screen).slice(0,rows):screen;
    shown.forEach((line,i)=>line.forEach((cell,j)=>drawCell(cell,j,i)));
    if(viewOff){
      const tag=' SCROLLBACK -'+viewOff+' ';
      [...tag].forEach((ch,i)=>drawCell({ch,ink:0,paper:14},cols-tag.length+i,0));
      return;
    }
    if(!connected)return;
    const raw=capture.textContent||'';
    const draft=secret?'*'.repeat(raw.length):raw;
    const start=Math.max(0,draft.length-(cols-1-x));
    let col=x;
    for(const ch of draft.slice(start))drawCell({ch,ink:15,paper:bg},col++,y);
    ctx.fillStyle=palette[10];ctx.fillRect(col*w,y*h+h-3,w,3);
    capture.style.left=(col/cols*100)+'%';
    capture.style.top=(y/rows*100)+'%';
  });
}
// Carrier: a WebSocket when it opens, HTTP polling until then and as fallback.
let sock=null,sockReady=false,sockTries=0,held=[];
function connectText(){return sockReady?'KOUVOSTO // CONNECT 28800/ARQ':'KOUVOSTO // CONNECT 2400';}
function frameIn(d){
  lastOk=Date.now();
  const k=d[0],v=d.slice(1);
  if(k==='o')render(v);
  else if(k==='p'){secret=v==='1';repaint();}
  else if(k==='s'){streamOn=parseInt(v,10)||0;if(streamOn!==2){closeViewer();closePlayer();}if(streamOn&&viewOff){viewOff=0;repaint();}}
  else if(k==='x')lost('KOUVOSTO // NO CARRIER');
}
function flushPending(){
  if(!sockReady||pending===null)return;
  sock.send('k'+pending+'\r');pending=null;enter.disabled=false;
}
function dropSocket(){
  if(!sock)return;
  const s=sock;sock=null;sockReady=false;
  s.onclose=s.onmessage=s.onerror=null;
  try{s.close();}catch{}
}
function openSocket(){
  if(!connected||sock||!window.WebSocket)return;
  let s;
  try{s=new WebSocket('ws://'+location.host+'/ws');}catch{return;}
  sock=s;
  const giveUp=setTimeout(()=>{if(!sockReady)s.close();},4000);
  s.onopen=()=>{
    clearTimeout(giveUp);
    s.send('a'+node+':'+token);
    s.send('b'+speed);
    s.send('c'+localTime());
    sockReady=true;sockTries=0;clearTimeout(timer);lastOk=Date.now();
    carrier.textContent=connectText();
    if(!busy)flushPending();
  };
  s.onmessage=e=>{if(busy)held.push(e.data);else frameIn(e.data);};
  s.onclose=()=>{
    clearTimeout(giveUp);
    const was=sockReady;sock=null;sockReady=false;
    if(!connected)return;
    if(was)lastOk=Date.now();
    carrier.textContent=connectText();wake(0);
    if(++sockTries<=3)setTimeout(openSocket,was?1000:5000);
  };
}
// Reconnect: this tab remembers its node and screen, so a dropped link, a
// sleeping phone or a reload rejoins the same node. The board keeps a quiet
// node for three minutes; after that it answers 404 and we say NO CARRIER.
const CARRIER_MS=180000;
let lastOk=0,saveTimer=0;
function store(key,value){try{if(value===null)sessionStorage.removeItem(key);else sessionStorage.setItem(key,JSON.stringify(value));}catch{}}
function recall(key){try{return JSON.parse(sessionStorage.getItem(key)||'null');}catch{return null;}}
function saveScreen(){
  if(saveTimer||!connected)return;
  saveTimer=setTimeout(()=>{
    saveTimer=0;if(!connected)return;
    const rows=screen.map(line=>[line.map(c=>c.ch).join(''),line.map(c=>(c.ink*16+c.paper).toString(16).padStart(2,'0')).join('')]);
    store('kouvosto.screen',{rows,x,y,fg,bg,bright,reverse});
  },700);
}
function restoreScreen(){
  const s=recall('kouvosto.screen');
  if(!s||!Array.isArray(s.rows)||s.rows.length!==rows)return;
  screen=s.rows.map(([chars,attrs])=>Array.from({length:cols},(_,i)=>{let a=parseInt(attrs.substr(i*2,2),16);if(Number.isNaN(a))a=0x70;return {ch:chars[i]||' ',ink:a>>4,paper:a&15};}));
  ({x,y,fg,bg,bright,reverse}=s);
}
function attach(){
  connected=true;failures=0;sockTries=0;held=[];lastOk=Date.now();
  enter.disabled=back.disabled=false;dial.disabled=true;
  store('kouvosto.node',{node,token});
  focusTerminal();wake(0);openSocket();sendSpeed();repaint();
}
function lost(message){
  dropSocket();held=[];
  store('kouvosto.node',null);store('kouvosto.screen',null);
  connected=false;pending=null;clearTimeout(timer);
  carrier.textContent=message;dial.disabled=false;enter.disabled=back.disabled=true;
  capture.textContent='';capture.blur();repaint();
}
// The phone's local time; the board may pull its invented clock forward to it.
function localTime(){const d=new Date(),p=n=>String(n).padStart(2,'0');return d.getFullYear()+'-'+p(d.getMonth()+1)+'-'+p(d.getDate())+' '+p(d.getHours())+':'+p(d.getMinutes())+':'+p(d.getSeconds());}
async function request(path,options={}){
  const abort=new AbortController();
  const timeout=setTimeout(()=>abort.abort(),7000);
  try{
    const response=await fetch(path,{...options,cache:'no-store',signal:abort.signal});
    return {status:response.status,text:await response.text(),secret:response.headers.get('X-BBS-Password')==='1',stream:parseInt(response.headers.get('X-BBS-Stream'),10)||0};
  }finally{clearTimeout(timeout);}
}
function wake(delay){
  clearTimeout(timer);
  if(connected)timer=setTimeout(exchange,delay);
}
async function exchange(){
  if(busy||!connected||sockReady)return;
  busy=true;
  const line=pending;
  pending=null;
  let delay=550;
  try{
    const params=new URLSearchParams({n:node,q:token});
    const response=line===null
      ? await request('/api/out?'+params)
      : await request('/api/in',{method:'POST',body:new URLSearchParams({n:node,q:token,k:line+'\r'})});
    if(response.status===404){lost('KOUVOSTO // NO CARRIER');return;}
    if(response.status!==200&&response.status!==204)throw Error('HTTP '+response.status);
    failures=0;lastOk=Date.now();
    carrier.textContent=connectText();
    if(!sockReady){secret=response.secret;streamOn=response.stream;if(streamOn!==2){closeViewer();closePlayer();}}
    if(response.text)render(response.text);
  }catch{
    // Keep trying quietly while the phone rejoins the WiFi. Give up only
    // once the board would have dropped the node anyway.
    failures++;
    if(Date.now()-lastOk>CARRIER_MS+5000){lost('KOUVOSTO // NO CARRIER');return;}
    carrier.textContent=line===null?'KOUVOSTO // RECONNECTING':'KOUVOSTO // COMMAND STATUS UNKNOWN';
    delay=Math.min(950*failures,5000);
  }finally{
    busy=false;
    const queued=held;held=[];queued.forEach(frameIn);
    if(connected&&sockReady)flushPending();
    else if(connected){enter.disabled=pending!==null;wake(pending!==null?0:delay);}
  }
}
function focusTerminal(){if(connected)capture.focus({preventScroll:true});}
if(window.visualViewport){
  const fit=()=>document.documentElement.style.setProperty('--visible-height',window.visualViewport.height+'px');
  window.visualViewport.addEventListener('resize',fit);
  fit();
}
// While a picture streams, a key goes out at once: space pauses, anything
// else (or a tap on the screen) skips to the prompt.
function streamKey(ch){
  if(!connected)return;
  viewOff=0;capture.textContent='';repaint();
  if(sockReady){sock.send('k'+ch);return;}
  if(pending===null){pending=ch;if(!busy)wake(0);}
}
function submit(){
  if(streamOn){streamKey(streamOn===2?'\r':'\x1b');return;}
  viewOff=0;
  if(!connected||pending!==null)return;
  pending=(capture.textContent||'').replace(/[\r\n]/g,'').slice(0,900);
  capture.textContent='';
  if(sockReady&&!busy){flushPending();repaint();return;}
  enter.disabled=true;
  repaint();if(!busy)wake(0);
}
dial.onclick=async()=>{
  dial.disabled=true;
  try{
    const response=await request('/api/connect',{method:'POST',body:new URLSearchParams({t:localTime()})});
    if(response.status!==200){lost(response.status===503?'KOUVOSTO // ALL NODES BUSY':'KOUVOSTO // NO CARRIER');return;}
    ({node,token}=JSON.parse(response.text));
    clear();move(0,0);partial='';fg=7;bg=0;bright=reverse=secret=false;
    carrier.textContent=connectText();attach();
  }catch{lost('KOUVOSTO // LINK ERROR');}
};
// Waking the phone or returning to the tab reconnects at once.
document.addEventListener('visibilitychange',()=>{
  if(document.visibilityState!=='visible'||!connected)return;
  if(!sockReady){sockTries=0;openSocket();}
  if(!busy&&!sockReady)wake(0);
});
// In the pager a tap turns the page; the bottom quarter of the screen goes back.
display.addEventListener('click',e=>{
  if(dragged)return;
  if(streamOn===2){const r=display.getBoundingClientRect();streamKey(e.clientY>r.top+r.height*0.75?'b':' ');focusTerminal();}
  else if(streamOn===1)streamKey('\x1b');
  else focusTerminal();
});
capture.addEventListener('input',()=>{
  if(streamOn===2){const t=capture.textContent||'';capture.textContent='';if(t)streamKey(t.slice(-1));return;}
  if(streamOn)streamKey('\x1b');else repaint();
});
const PAGER_KEYS={ArrowUp:'\x1b[A',ArrowDown:'\x1b[B',PageUp:'\x1b[5~',PageDown:'\x1b[6~',Home:'\x1b[H',End:'\x1b[F',Escape:'q',Enter:'\r'};
capture.addEventListener('keydown',e=>{
  if((!viewer.hidden||playerOpen())&&(e.key==='ArrowRight'||e.key==='ArrowLeft')){e.preventDefault();streamKey(e.key==='ArrowRight'?'n':'p');return;}
  if(playerOpen()&&e.key===' '){e.preventDefault();window.TinyPlayer.togglePause();return;}
  if(playerOpen()&&(e.key==='ArrowUp'||e.key==='ArrowDown')&&window.TinyPlayer.subsong(e.key==='ArrowUp'?1:-1)){e.preventDefault();return;}
  if(streamOn===2&&!e.isComposing){
    const k=PAGER_KEYS[e.key]||(e.key.length===1&&!e.ctrlKey&&!e.metaKey?e.key:null);
    if(k){e.preventDefault();streamKey(k);}
    return;
  }
  if(streamOn&&!e.isComposing&&e.key.length===1||streamOn&&e.key==='Enter'){e.preventDefault();streamKey(e.key===' '?' ':'\x1b');return;}
  if(e.key==='Enter'&&!e.isComposing){e.preventDefault();submit();}
});
// Scrollback: mouse wheel or a finger drag on the terminal.
let dragY=null,dragOff=0,dragged=false;
const lineHeight=()=>display.clientHeight/rows;
// No scrollback while the board's reader is open or a picture plays: the
// reader does its own scrolling, and history holds earlier screens.
function scrollTo(off){viewOff=streamOn?0:Math.max(0,Math.min(history.length,Math.round(off)));repaint();}
// In the pager the wheel scrolls the text a line at a time and a swipe turns pages.
display.addEventListener('wheel',e=>{
  e.preventDefault();
  if(streamOn===2){streamKey(e.deltaY<0?'\x1b[A':'\x1b[B');return;}
  scrollTo(viewOff+(e.deltaY<0?3:-3));
},{passive:false});
display.addEventListener('touchstart',e=>{dragY=e.touches[0].clientY;dragOff=viewOff;dragged=false;},{passive:true});
display.addEventListener('touchmove',e=>{
  if(dragY===null)return;
  const dy=(e.touches[0].clientY-dragY)/lineHeight();
  if(Math.abs(dy)>0.5){dragged=true;e.preventDefault();if(streamOn!==2)scrollTo(dragOff+dy);}
},{passive:false});
display.addEventListener('touchend',e=>{
  if(streamOn===2&&dragged&&dragY!==null){
    const dy=(e.changedTouches[0].clientY-dragY)/lineHeight();
    if(Math.abs(dy)>2)streamKey(dy>0?'b':' ');
  }
  dragY=null;setTimeout(()=>dragged=false,50);
});
capture.addEventListener('beforeinput',e=>{
  if(e.inputType==='insertParagraph'||e.inputType==='insertLineBreak'){e.preventDefault();submit();}
});
enter.onclick=()=>{submit();focusTerminal();};
back.onclick=()=>{capture.textContent=(capture.textContent||'').slice(0,-1);focusTerminal();repaint();};
 modem.onclick=playModem;
// SPEED: the modem speed ANSI pictures play at (V in a file area). Kept in
// this browser and sent to the board whenever we attach.
const speedBtn=$('speed'),SPEEDS=[2400,9600,14400,28800,57600,0];
let speed=28800;
try{const v=parseInt(localStorage.getItem('kouvosto.speed'),10);if(SPEEDS.includes(v))speed=v;}catch{}
function showSpeed(){speedBtn.textContent='SPEED '+(speed||'MAX');}
function sendSpeed(){
  if(!connected)return;
  if(sockReady)sock.send('b'+speed);
  else fetch('/api/speed',{method:'POST',body:new URLSearchParams({n:node,q:token,b:speed})}).catch(()=>{});
}
speedBtn.onclick=()=>{
  speed=SPEEDS[(SPEEDS.indexOf(speed)+1)%SPEEDS.length];
  try{localStorage.setItem('kouvosto.speed',String(speed));}catch{}
  showSpeed();sendSpeed();
};
showSpeed();
// No hangup when the page unloads: a reload must be able to rejoin the node.
// G (logoff) hangs up at once; an abandoned tab frees its node after the
// three-minute carrier timeout, like a dropped line.
(()=>{
  const saved=recall('kouvosto.node');
  if(!saved||!saved.node||!saved.token)return;
  node=saved.node;token=saved.token;
  restoreScreen();
  carrier.textContent='KOUVOSTO // RECONNECTING';
  attach();
})();
repaint();
</script></body></html>)TERM";
