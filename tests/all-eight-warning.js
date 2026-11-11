const { chromium } = require('playwright');
(async()=>{const b=await chromium.launch();const c=await b.newContext({viewport:{width:1400,height:1000}});const p=await c.newPage();
p.on('pageerror',e=>console.log('ERR',e.message));
const ids=["purplegrand","purpleep2","polypad","multistrings","dminus22","zdkstrings","synbrass2","80spopbrass"];
await p.addInitScript(ids=>{ if(!localStorage.getItem('x')){localStorage.setItem('x','1');const o={};ids.forEach(i=>o[i]=true);localStorage.setItem('psb.on',JSON.stringify(o));localStorage.setItem('psb.single','false');localStorage.setItem('psb.off','[]');}},ids);
await p.goto('file:///home/claude/purplesoundbank/index.html');await p.waitForTimeout(4000);
console.log('on pads',await p.$$eval('#pads .pad.on',e=>e.length),'warn',!!(await p.$('#psbWarn')));
await p.click('#psbWarn button');console.log('dismissed',!(await p.$('#psbWarn')));
await b.close();})();
