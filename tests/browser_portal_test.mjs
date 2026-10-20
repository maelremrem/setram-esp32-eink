#!/usr/bin/env node
import {spawn} from 'node:child_process';
import {mkdtemp, rm, mkdir, writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

const root = new URL('..', import.meta.url).pathname.replace(/\/$/, '');
const port = 18765 + Math.floor(Math.random()*400);
const cdpPort = 19265 + Math.floor(Math.random()*400);
const server = spawn('python', ['tests/serve_bus_portal.py', String(port)], {cwd:root, stdio:'ignore'});
const profile = await mkdtemp(join(tmpdir(), 'bus-notifier-chrome-'));
const chrome = spawn('google-chrome', ['--headless=new','--no-sandbox','--disable-gpu',
  `--remote-debugging-port=${cdpPort}`, `--user-data-dir=${profile}`, 'about:blank'], {stdio:'ignore'});

async function waitFor(url, predicate, timeout=10000) {
  const deadline=Date.now()+timeout;
  while(Date.now()<deadline) { try { const value=await (await fetch(url)).json(); if(predicate(value)) return value; } catch {}
    await new Promise(resolve=>setTimeout(resolve,100)); }
  throw new Error(`Timed out waiting for ${url}`);
}
async function cdp(ws, method, params={}) {
  const id=++cdp.nextId;
  return await new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>reject(new Error(`CDP timeout ${method}`)),10000);
    cdp.pending.set(id,{resolve,reject,timer,method}); ws.send(JSON.stringify({id,method,params}));
  });
}
cdp.nextId=0; cdp.pending=new Map();
let ws;
try {
  await waitFor(`http://127.0.0.1:${port}/api/current`, x=>x.stop==='');
  const target=await waitFor(`http://127.0.0.1:${cdpPort}/json/list`, x=>Array.isArray(x)&&x.some(t=>t.type==='page'));
  const page=target.find(t=>t.type==='page');
  ws=new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((resolve,reject)=>{ws.onopen=resolve; ws.onerror=reject;});
  ws.onmessage=event=>{const message=JSON.parse(event.data);const pending=cdp.pending.get(message.id);if(!pending)return;clearTimeout(pending.timer);cdp.pending.delete(message.id);if(message.error){console.error(`CDP ${pending.method}: ${JSON.stringify(message.error)}`);pending.reject(new Error(JSON.stringify(message.error)));}else pending.resolve(message.result);};
  await cdp(ws,'Page.enable');
  await cdp(ws,'Runtime.enable');
  try { await cdp(ws,'Page.navigate',{url:`http://127.0.0.1:${port}/bus`}); } catch(error) {
    if(!String(error).includes('Inspected target navigated or closed')) throw error;
  }
  await new Promise(resolve=>setTimeout(resolve,300));
  async function evaluate(expression) {
    for(let attempt=0;;attempt++) {
      try {
        const result=await cdp(ws,'Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});
        if(result.exceptionDetails) throw new Error(result.exceptionDetails.text||'page evaluation failed');
        return result.result.value;
      } catch(error) {
        if(attempt>=10 || !String(error).includes('Inspected target navigated or closed')) throw error;
        await new Promise(resolve=>setTimeout(resolve,200));
      }
    }
  }
  await evaluate(`new Promise(resolve=>{const done=()=>document.querySelector('#search')&&!document.querySelector('#search').disabled; if(done())resolve(); else {const o=new MutationObserver(()=>{if(done()){o.disconnect();resolve();}});o.observe(document.body,{subtree:true,attributes:true});}})`);
  const allLines=await evaluate(`(async()=>{const data=await (await fetch('/api/catalogue')).json();const expected=[...new Set(data.stops.flatMap(s=>s.services.map(x=>x.line)))].sort();const buttons=[...document.querySelectorAll('#lines button[data-line]')];return {expected,actual:buttons.map(b=>b.dataset.line).filter(Boolean).sort(),visible:buttons.every(b=>b.getBoundingClientRect().width>0),order:document.querySelector('#lines')?.compareDocumentPosition(document.querySelector('#search'))&Node.DOCUMENT_POSITION_FOLLOWING}})()`);
  if(JSON.stringify(allLines.expected)!==JSON.stringify(allLines.actual)||!allLines.visible||!allLines.order) throw new Error(`all catalogue lines must be visible before stop search: ${JSON.stringify(allLines)}`);
  const lineSearch=await evaluate(`(async()=>{const data=await (await fetch('/api/catalogue')).json();document.querySelector('#lines button[data-line="C4"]').click();const expected=data.stops.filter(s=>s.services.some(x=>x.line==='C4'));const actual=[...document.querySelectorAll('#stop option')].map(o=>o.value).filter(Boolean);if(JSON.stringify(expected.map(s=>s.id))!==JSON.stringify(actual))return false;const multi=expected.find(s=>s.services.some(x=>x.line!=='C4'));const stop=document.querySelector('#stop');stop.value=multi.id;stop.dispatchEvent(new Event('change'));if([...document.querySelectorAll('#service label')].some(l=>!l.textContent.trim().startsWith('C4 ·')))return false;document.querySelector('#lines button[data-line=""]').click();const query='republique';const input=document.querySelector('#search');input.value=query;input.dispatchEvent(new Event('input'));const norm=s=>s.normalize('NFD').replace(/[\\u0300-\\u036f]/g,'').toLowerCase();const accented=data.stops.filter(s=>norm(s.name+' '+s.id).includes(query)).map(s=>s.id);const found=[...document.querySelectorAll('#stop option')].map(o=>o.value).filter(Boolean);input.value='';input.dispatchEvent(new Event('input'));return accented.length>0&&JSON.stringify(accented)===JSON.stringify(found)&&document.querySelectorAll('#stop option').length===data.stops.length+1})()`);
  if(!lineSearch) throw new Error('line-first stop/services filtering, accent search or complete global results failed');
  const first=await evaluate(`(()=>{document.querySelector('#lines button[data-line="13"]').click();const s=document.querySelector('#search');s.value='2002';s.dispatchEvent(new Event('input',{bubbles:true}));const stop=document.querySelector('#stop');stop.value='2002';stop.dispatchEvent(new Event('change',{bubbles:true}));const c=[...document.querySelectorAll('#service input')][0];c.click();return [...document.querySelectorAll('#service input')].map(x=>x.checked)})()`);
  if(!first[0]) throw new Error('first tuple was not selected');
  const second=await evaluate(`(()=>{document.querySelector('#lines button[data-line="C4"]').click();const s=document.querySelector('#search');s.value='2003';s.dispatchEvent(new Event('input',{bubbles:true}));const stop=document.querySelector('#stop');stop.value='2003';stop.dispatchEvent(new Event('change',{bubbles:true}));const c=[...document.querySelectorAll('#service input')][0];c.click();return document.querySelector('#secondStopValue').value})()`);
  if(second!=='2003') throw new Error('second tuple was not retained across search');
  const submitted=await evaluate(`(async()=>{document.querySelector('#save').click();for(let i=0;i<100;i++){if(document.querySelector('#status').textContent.includes('validée'))break;await new Promise(r=>setTimeout(r,20))}return {status:document.querySelector('#status').textContent.includes('validée')?200:0,current:await (await fetch('/api/current')).json()}})()`);
  if(submitted.status!==200 || submitted.current.stop!=='2002' || submitted.current.second_stop!=='2003' || submitted.current.second_stop_label!=='Acacias') throw new Error(`stateful dual POST/current roundtrip failed: ${JSON.stringify(submitted)}`);
  const duplicate=await evaluate(`(async()=>{const p=new URLSearchParams(new FormData(document.querySelector('#catalogueForm')));p.set('second_stop','2002');p.set('second_route','13');p.set('second_direction','0');p.set('second_destination','MAIRIE ARNAGE');const r=await fetch('/api/settings',{method:'POST',body:p});return r.status})()`);
  if(duplicate!==400) throw new Error(`duplicate catalogue tuple was accepted: ${duplicate}`);
  try { await cdp(ws,'Runtime.evaluate',{expression:'location.reload()'}); } catch(error) {
    if(!String(error).includes('Inspected target navigated or closed')) throw error;
  }
  await new Promise(resolve=>setTimeout(resolve,300));
  for(let attempt=0;;attempt++) {
    const ready=await evaluate(`document.querySelector('#search')&&!document.querySelector('#search').disabled&&document.querySelector('#secondStopValue').value==='2003'`);
    if(ready) break;
    if(attempt>=50) throw new Error('reload did not restore selected state');
    await new Promise(resolve=>setTimeout(resolve,100));
  }
  const restored=await evaluate(`({first:document.querySelector('#stopValue').value,second:document.querySelector('#secondStopValue').value,labels:document.querySelector('#chosen').textContent,checked:[...document.querySelectorAll('#service input')].map(x=>x.checked),checkboxWidth:getComputedStyle(document.querySelector('#service input')).width,lineFilter:!!document.querySelector('#lineFilter')})`);
  const restoredLines=await evaluate(`({search:document.querySelector('#search').value,lines:[...document.querySelectorAll('#lines button')].map(b=>b.dataset.line).filter(Boolean).sort(),allStops:document.querySelectorAll('#stop option').length})`);
  if(restoredLines.search!==''||JSON.stringify(restoredLines.lines)!==JSON.stringify(allLines.expected)||restoredLines.allStops<=81) throw new Error(`reload hid complete lines/stops: ${JSON.stringify(restoredLines)}`);
  await mkdir(join(root,'.cache'),{recursive:true});
  for(const [name,width,height,mobile] of [['desktop',1280,1000,false],['mobile',360,800,true]]) {
    await cdp(ws,'Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile});
    const layout=await evaluate(`({width:innerWidth,content:document.documentElement.scrollWidth,lines:document.querySelectorAll('#lines button[data-line]').length})`);
    if(layout.content>width) throw new Error(`horizontal overflow ${name}: ${JSON.stringify(layout)}`);
    const metrics=await cdp(ws,'Page.getLayoutMetrics');
    const shot=await cdp(ws,'Page.captureScreenshot',{format:'png',captureBeyondViewport:true,clip:{x:0,y:0,width,height:Math.ceil(metrics.cssContentSize.height),scale:1}});
    await writeFile(join(root,'.cache',`portal-${name}.png`),Buffer.from(shot.data,'base64'));
  }
  if(restored.first!=='2002'||restored.second!=='2003'||!restored.labels.includes('Abattoirs')||!restored.labels.includes('Acacias')||!restored.lineFilter||restored.checkboxWidth==='100%') throw new Error(`restore/css/filter failed: ${JSON.stringify(restored)}`);
  const filtered=await evaluate(`(()=>{const s=document.querySelector('#search');s.value='';s.dispatchEvent(new Event('input',{bubbles:true}));const f=document.querySelector('#lineFilter');f.value='C4';f.dispatchEvent(new Event('change',{bubbles:true}));return [...document.querySelectorAll('#stop option')].map(x=>x.value)})()`);
  if(!filtered.includes('2003')||filtered.includes('2002')) throw new Error(`line filter did not filter physical quays: ${JSON.stringify(filtered)}`);
  await evaluate(`(()=>{const f=document.querySelector('#lineFilter');f.value='';f.dispatchEvent(new Event('change',{bubbles:true}));})()`);
  const sameStop=await evaluate(`(()=>{document.querySelector('#chosen button').click();const s=document.querySelector('#search');s.value='2003';s.dispatchEvent(new Event('input',{bubbles:true}));const stop=document.querySelector('#stop');stop.value='2003';stop.dispatchEvent(new Event('change',{bubbles:true}));const boxes=[...document.querySelectorAll('#service input')];boxes[1].click();return {checked:boxes.map(x=>x.checked),disabled:boxes.map(x=>x.disabled),chosen:document.querySelector('#chosen').textContent}})()`);
  if(!sameStop.checked.every(Boolean)||sameStop.disabled.some(Boolean)||!sameStop.chosen.includes('ST-JOSEPH')) throw new Error(`same-stop distinct service failed: ${JSON.stringify(sameStop)}`);
  const third=await evaluate(`(()=>{const s=document.querySelector('#search');s.value='2002';s.dispatchEvent(new Event('input',{bubbles:true}));const stop=document.querySelector('#stop');stop.value='2002';stop.dispatchEvent(new Event('change',{bubbles:true}));const box=document.querySelector('#service input');return {disabled:box.disabled,before:document.querySelectorAll('#chosen p').length,checked:box.checked}})()`);
  if(!third.disabled||third.checked||third.before!==2) throw new Error(`max2 third reject/sync failed: ${JSON.stringify(third)}`);
  console.log('Browser portal behavioral tests: PASS (all catalogue lines visible, line-first stop/service search, accents, full results, dual POST/current/reload, same-stop services, max2, desktop/mobile without overflow)');
} finally {
  if(ws) ws.close();
  const stop=(child)=>new Promise(resolve=>{if(child.exitCode!==null)return resolve();child.once('exit',resolve);child.kill('SIGTERM');setTimeout(()=>{if(child.exitCode===null)child.kill('SIGKILL');},1000);});
  await Promise.all([stop(chrome),stop(server)]);
  await rm(profile,{recursive:true,force:true,maxRetries:10,retryDelay:100});
}
