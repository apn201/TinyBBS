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
touch-action:manipulation}
#capture{position:absolute;width:1.2ch;height:1em;overflow:hidden;color:transparent;
caret-color:transparent;outline:none;white-space:pre;pointer-events:none}
#links:empty{display:none} a{color:#5ff}
</style></head><body>
 <header><button id="dial">DIAL</button> <button id="modem" type="button">MODEM AUDIO</button> <span id="carrier" role="status">KOUVOSTO // NO CARRIER</span></header>
<div id="screen"><canvas id="display" width="640" height="400" aria-label="80-column Kouvosto terminal"></canvas>
<div id="capture" contenteditable="plaintext-only" role="textbox" aria-label="Terminal input"
autocapitalize="off" autocorrect="off" spellcheck="false" inputmode="text"
data-lpignore="true" data-1p-ignore data-bwignore></div></div>
<footer><button id="enter" disabled>ENTER</button> <button id="back" disabled>BACKSPACE</button>
<span id="links"></span></footer>
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
  else{screen.shift();screen.push(row());}
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
function downloadLink(message){
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
        while(i<data.length&&!/[@-~]/.test(data[i]))i++;
        if(i===data.length){partial=data.slice(start);break;}
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
  if(cell.ch==='█')ctx.fillRect(px,py,w,h);
  else if(cell.ch==='▀')ctx.fillRect(px,py,w,h/2);
  else if(cell.ch==='▄')ctx.fillRect(px,py+h/2,w,h/2);
  else if(cell.ch===' ' )return;
  else ctx.fillText(cell.ch,px,py+13,w);
}
function repaint(){
  if(frame)return;
  frame=requestAnimationFrame(()=>{
    frame=0;
    ctx.font='14px "Courier New", monospace';
    screen.forEach((line,i)=>line.forEach((cell,j)=>drawCell(cell,j,i)));
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
function lost(message){
  connected=false;pending=null;clearTimeout(timer);
  carrier.textContent=message;dial.disabled=false;enter.disabled=back.disabled=true;
  capture.textContent='';capture.blur();repaint();
}
async function request(path,options={}){
  const abort=new AbortController();
  const timeout=setTimeout(()=>abort.abort(),7000);
  try{
    const response=await fetch(path,{...options,cache:'no-store',signal:abort.signal});
    return {status:response.status,text:await response.text(),secret:response.headers.get('X-BBS-Password')==='1'};
  }finally{clearTimeout(timeout);}
}
function wake(delay){
  clearTimeout(timer);
  if(connected)timer=setTimeout(exchange,delay);
}
async function exchange(){
  if(busy||!connected)return;
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
    failures=0;
    carrier.textContent='KOUVOSTO // CONNECT';
    secret=response.secret;
    if(response.text)render(response.text);
  }catch{
    if(++failures>=3){lost('KOUVOSTO // LINK LOST');return;}
    carrier.textContent=line===null?'KOUVOSTO // RETRYING':'KOUVOSTO // COMMAND STATUS UNKNOWN';
    delay=950;
  }finally{
    busy=false;
    if(connected){enter.disabled=pending!==null;wake(pending!==null?0:delay);}
  }
}
function focusTerminal(){if(connected)capture.focus({preventScroll:true});}
if(window.visualViewport){
  const fit=()=>document.documentElement.style.setProperty('--visible-height',window.visualViewport.height+'px');
  window.visualViewport.addEventListener('resize',fit);
  fit();
}
function submit(){
  if(!connected||pending!==null)return;
  pending=(capture.textContent||'').replace(/[\r\n]/g,'').slice(0,900);
  capture.textContent='';enter.disabled=true;
  repaint();if(!busy)wake(0);
}
dial.onclick=async()=>{
  dial.disabled=true;
  try{
    const response=await request('/api/connect',{method:'POST'});
    if(response.status!==200){lost(response.status===503?'KOUVOSTO // ALL NODES BUSY':'KOUVOSTO // NO CARRIER');return;}
    ({node,token}=JSON.parse(response.text));
    clear();move(0,0);partial='';fg=7;bg=0;bright=reverse=secret=false;failures=0;
    connected=true;enter.disabled=back.disabled=false;
    carrier.textContent='KOUVOSTO // CONNECT';focusTerminal();wake(0);repaint();
  }catch{lost('KOUVOSTO // LINK ERROR');}
};
display.addEventListener('click',focusTerminal);
capture.addEventListener('input',repaint);
capture.addEventListener('keydown',e=>{
  if(e.key==='Enter'&&!e.isComposing){e.preventDefault();submit();}
});
capture.addEventListener('beforeinput',e=>{
  if(e.inputType==='insertParagraph'||e.inputType==='insertLineBreak'){e.preventDefault();submit();}
});
enter.onclick=()=>{submit();focusTerminal();};
back.onclick=()=>{capture.textContent=(capture.textContent||'').slice(0,-1);focusTerminal();repaint();};
 modem.onclick=playModem;
addEventListener('pagehide',e=>{
  if(connected&&!e.persisted)navigator.sendBeacon('/api/hangup',new URLSearchParams({n:node,q:token}));
});
repaint();
</script></body></html>)TERM";
