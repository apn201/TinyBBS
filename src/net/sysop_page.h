#pragma once
// The SysOp file manager page. Deliberately modern and plain: this is the
// admin backstage, not part of the period BBS.

static const char kSysopPage[] PROGMEM = R"PAGE(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Kouvosto // File manager</title>
<style>
:root{--bg:#111;--fg:#ddd;--dim:#888;--line:#333;--acc:#5ff;--bad:#f77}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px system-ui,sans-serif}
header{position:sticky;top:0;background:#000;padding:10px 12px;border-bottom:1px solid var(--line);z-index:2}
h1{margin:0 0 6px;font:600 14px monospace;color:var(--acc)}
#path{font:13px monospace;word-break:break-all}
#path a{color:var(--acc);text-decoration:none}
.bar{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px}
button,label.btn{background:#222;color:var(--fg);border:1px solid #444;border-radius:4px;padding:7px 10px;font:inherit;font-size:14px;cursor:pointer}
button:disabled{opacity:.4;cursor:default}
#status{margin-top:6px;font:13px monospace;color:var(--dim);min-height:1.2em}
#status.bad{color:var(--bad)}
.ro{color:var(--bad);font:12px monospace;margin-left:6px}
table{width:100%;border-collapse:collapse}
td{padding:8px 12px;border-bottom:1px solid var(--line);vertical-align:middle}
td.n{word-break:break-all}
td.n a{color:var(--fg);text-decoration:none}
td.n a.dir{color:var(--acc);font-weight:600}
td.s{text-align:right;color:var(--dim);font:13px monospace;white-space:nowrap}
td.a{text-align:right;white-space:nowrap}
td.a button{padding:4px 8px;font-size:13px;margin-left:4px}
#drop{display:none;position:fixed;inset:0;background:rgba(0,80,80,.6);color:#fff;font:600 22px monospace;align-items:center;justify-content:center;z-index:5}
body.dragging #drop{display:flex}
#editor{display:none;position:fixed;inset:0;background:var(--bg);z-index:4;flex-direction:column}
#editor.open{display:flex}
#editor .bar{padding:10px 12px;margin:0;border-bottom:1px solid var(--line);align-items:center}
#editor b{font:13px monospace;flex:1;word-break:break-all}
textarea{flex:1;width:100%;background:#000;color:#ddd;border:0;padding:10px;font:13px/1.35 monospace;resize:none;white-space:pre;overflow:auto}
input[type=file]{display:none}
</style></head><body>
<header>
<h1>KOUVOSTO // SYSOP FILE MANAGER</h1>
<div id="path"></div>
<div class="bar">
<button id="up">Up</button>
<button id="mkdir">New folder</button>
<label class="btn" id="upf">Upload files<input id="files" type="file" multiple></label>
<label class="btn" id="upd">Upload folder<input id="folder" type="file" webkitdirectory multiple></label>
<button id="refresh">Refresh</button>
</div>
<div id="status"></div>
</header>
<table><tbody id="list"></tbody></table>
<div id="drop">Drop files or folders to upload here</div>
<div id="editor"><div class="bar"><b id="ename"></b><button id="esave">Save</button><button id="eclose">Close</button></div>
<textarea id="etext" spellcheck="false" autocapitalize="off" autocomplete="off"></textarea></div>
<script>
'use strict';
const $=id=>document.getElementById(id);
const T=new URLSearchParams(location.search).get('t')||'';
const TEXT=/\.(ini|txt|ans|asc|bbs|ion|diz|nfo|md|cfg|json|csv|bas|sdi)$/i;
let cwd='/BBS',ro=false,busy=false,editing=null;
const qs=(p,extra={})=>'?'+new URLSearchParams({t:T,p,...extra});
const join=(a,b)=>b?a+'/'+b:a;
function status(msg,bad){const s=$('status');s.textContent=msg||'';s.className=bad?'bad':'';}
async function api(path,opt){
  const r=await fetch(path,{cache:'no-store',...opt});
  if(!r.ok)throw Error((await r.text())||('HTTP '+r.status));
  return r;
}
function size(n){return n<1024?n+' B':n<1048576?(n/1024).toFixed(1)+' K':(n/1048576).toFixed(1)+' M';}
function crumbs(){
  const parts=cwd.split('/').filter(Boolean),box=$('path');box.replaceChildren();
  let acc='';
  parts.forEach((p,i)=>{acc+='/'+p;const a=document.createElement('a');a.href='#';a.textContent=p;const to=acc;a.onclick=e=>{e.preventDefault();load(to);};box.append('/',a);});
  if(ro){const s=document.createElement('span');s.className='ro';s.textContent='read-only';box.append(s);}
}
function btn(label,fn){const b=document.createElement('button');b.textContent=label;b.onclick=fn;b.disabled=ro;return b;}
function render(items){
  items.sort((a,b)=>b.d-a.d||a.n.localeCompare(b.n,undefined,{sensitivity:'base'}));
  const list=$('list');list.replaceChildren();
  if(!items.length){const tr=list.insertRow();const td=tr.insertCell();td.colSpan=3;td.textContent='Empty folder';td.style.color='#888';}
  for(const it of items){
    const tr=list.insertRow(),n=tr.insertCell(),s=tr.insertCell(),a=tr.insertCell();
    n.className='n';s.className='s';a.className='a';
    const link=document.createElement('a');link.textContent=it.n+(it.d?'/':'');
    if(it.d){link.className='dir';link.href='#';link.onclick=e=>{e.preventDefault();load(join(cwd,it.n));};}
    else{link.href='/sysop/api/get'+qs(join(cwd,it.n),{dl:'1'});}
    n.append(link);
    s.textContent=it.d?'':size(it.s);
    if(!it.d&&TEXT.test(it.n)){const e=btn('Edit',()=>edit(it.n,it.s));e.disabled=false;e.textContent=ro?'View':'Edit';a.append(e);}
    a.append(btn('Rename',()=>rename(it.n)),btn('Delete',()=>remove(it.n,it.d)));
  }
  $('up').disabled=cwd.toUpperCase()==='/BBS';
  for(const id of ['mkdir','upf','upd'])$(id).style.opacity=ro?.4:1;
  $('mkdir').disabled=ro;$('files').disabled=$('folder').disabled=ro;
}
async function load(p){
  try{const r=await api('/sysop/api/list'+qs(p));const j=await r.json();cwd=j.path;ro=j.ro;crumbs();render(j.items);status('');}
  catch(e){status(e.message,true);}
}
async function post(path,p,extra){return api(path+qs(p,extra),{method:'POST'});}
async function rename(name){
  const to=prompt('New name for '+name,name);if(!to||to===name)return;
  try{await post('/sysop/api/rename',join(cwd,name),{to});status('Renamed '+name+' to '+to);load(cwd);}catch(e){status(e.message,true);}
}
async function remove(name,dir){
  if(!confirm('Delete '+name+(dir?' and everything in it':'')+'? This cannot be undone.'))return;
  try{status('Deleting '+name+'...');await post('/sysop/api/delete',join(cwd,name));status('Deleted '+name);load(cwd);}catch(e){status(e.message,true);load(cwd);}
}
$('mkdir').onclick=async()=>{
  const name=prompt('New folder name');if(!name)return;
  try{await post('/sysop/api/mkdir',join(cwd,name));load(cwd);}catch(e){status(e.message,true);}
};
$('up').onclick=()=>load(cwd.slice(0,cwd.lastIndexOf('/'))||'/BBS');
$('refresh').onclick=()=>load(cwd);
function sendOne(file,dir,name,label){
  return new Promise((ok,fail)=>{
    const form=new FormData();form.append('f',file,name);
    const x=new XMLHttpRequest();
    x.open('POST','/sysop/api/upload'+qs(dir));
    x.upload.onprogress=e=>{if(e.lengthComputable)status(label+' '+Math.round(e.loaded*100/e.total)+'%');};
    x.onload=()=>x.status===200?ok():fail(Error(x.responseText||('HTTP '+x.status)));
    x.onerror=()=>fail(Error('Connection lost'));
    x.send(form);
  });
}
async function uploadAll(jobs){
  if(busy||!jobs.length)return;
  if(ro){status('This folder is read-only',true);return;}
  busy=true;let done=0;
  try{
    for(const j of jobs){await sendOne(j.file,j.dir,j.file.name,(++done)+'/'+jobs.length+' '+j.file.name);}
    status('Uploaded '+done+' file'+(done===1?'':'s'));
  }catch(e){status('Stopped after '+(done-1)+' of '+jobs.length+': '+e.message,true);}
  busy=false;load(cwd);
}
$('files').onchange=e=>{const f=[...e.target.files];e.target.value='';uploadAll(f.map(file=>({file,dir:cwd})));};
$('folder').onchange=e=>{
  const f=[...e.target.files];e.target.value='';
  uploadAll(f.map(file=>{const rel=file.webkitRelativePath||file.name;const cut=rel.lastIndexOf('/');return {file,dir:cut>0?join(cwd,rel.slice(0,cut)):cwd};}));
};
function walk(entry,dir,jobs){
  return new Promise(done=>{
    if(entry.isFile){entry.file(file=>{jobs.push({file,dir});done();},()=>done());return;}
    if(!entry.isDirectory){done();return;}
    const sub=join(dir,entry.name),reader=entry.createReader();
    const more=()=>reader.readEntries(async list=>{
      if(!list.length){done();return;}
      for(const e of list)await walk(e,sub,jobs);
      more();
    },()=>done());
    more();
  });
}
let depth=0;
addEventListener('dragenter',e=>{e.preventDefault();if(++depth===1)document.body.classList.add('dragging');});
addEventListener('dragleave',e=>{e.preventDefault();if(--depth<=0){depth=0;document.body.classList.remove('dragging');}});
addEventListener('dragover',e=>e.preventDefault());
addEventListener('drop',async e=>{
  e.preventDefault();depth=0;document.body.classList.remove('dragging');
  const jobs=[],entries=[...e.dataTransfer.items].map(i=>i.webkitGetAsEntry&&i.webkitGetAsEntry()).filter(Boolean);
  if(entries.length){status('Reading dropped items...');for(const en of entries)await walk(en,cwd,jobs);}
  else for(const file of e.dataTransfer.files)jobs.push({file,dir:cwd});
  uploadAll(jobs);
});
// Text files are loaded and saved byte for byte, so CP437 art survives an edit.
async function edit(name,bytes){
  if(bytes>262144){status('Too big to edit here (over 256 K). Download it instead.',true);return;}
  try{
    const r=await api('/sysop/api/get'+qs(join(cwd,name)));
    const buf=new Uint8Array(await r.arrayBuffer());let s='';
    for(let i=0;i<buf.length;i+=4096)s+=String.fromCharCode.apply(null,buf.subarray(i,i+4096));
    editing={name,dir:cwd};$('ename').textContent=join(cwd,name)+(ro?' (read-only)':'');
    $('etext').value=s;$('esave').disabled=ro;$('editor').classList.add('open');
  }catch(e){status(e.message,true);}
}
$('eclose').onclick=()=>{$('editor').classList.remove('open');editing=null;};
$('esave').onclick=async()=>{
  if(!editing)return;
  const text=$('etext').value,out=new Uint8Array(text.length);
  for(let i=0;i<text.length;i++)out[i]=text.charCodeAt(i)&255;
  try{
    $('esave').disabled=true;
    await sendOne(new Blob([out]),editing.dir,editing.name,'Saving '+editing.name);
    status('Saved '+editing.name);$('editor').classList.remove('open');editing=null;load(cwd);
  }catch(e){status(e.message,true);}
  finally{$('esave').disabled=ro;}
};
if(!T)status('No admin token. Open the file manager from the SysOp menu.',true);else load('/BBS');
</script></body></html>)PAGE";
