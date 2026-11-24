// Execute the actual captive-portal script with a deterministic DOM/API double.
// No browser, network, package installs or real Wi-Fi credentials are used.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const root=path.resolve(__dirname,'..'),header=fs.readFileSync(path.join(root,'display/include/portal_page.h'),'utf8'),html=header.split('R"VINYL(')[1].split(')VINYL"')[0],script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
assert(html.includes('<html lang="en">'));assert(!/[À-ỹ]/u.test(html));assert(!html.includes('innerHTML'));assert(html.includes('ESP Lyric Speaker'));assert(!html.includes('cdn.'));
const ids=[...html.matchAll(/\bid="([^"]+)"/g)].map(m=>m[1]);assert.equal(new Set(ids).size,ids.length);
class Element{constructor(value=''){this.value=value;this.textContent='';this.children=[];this.disabled=false;this.checked=false;this.dataset={};this.type='password';this.classes=new Set();this.classList={toggle:(name,on)=>on?this.classes.add(name):this.classes.delete(name)}}replaceChildren(...children){this.children=children}append(child){this.children.push(child)}}
function fixture(){
 const elements=Object.fromEntries(ids.map(id=>[id,new Element()])),timers=new Map(),calls=[];let next=1;
 let state={state:'ready',message:'',ssid:'',elapsed:0},list=[{ssid:'Home & Cafe',secure:true,supported:true},{ssid:'Open WiFi',secure:false,supported:true},{ssid:'<img src=x onerror=alert(1)>',secure:true,supported:true},{ssid:'Office',secure:true,supported:false}],scanning=false,reject=false;
 class Option extends Element{constructor(text,value){super(value);this.textContent=text}}
 const context=vm.createContext({document:{getElementById:id=>elements[id]},Option,AbortController,URLSearchParams,console,setTimeout:(fn,ms)=>{const id=next++;timers.set(id,{fn,ms});return id},clearTimeout:id=>timers.delete(id),fetch:async(url,options={})=>{calls.push({url,options});if(url==='/api/connect'){if(reject)return{ok:false,json:async()=>({message:'Check the password and try again.'})};state={state:'connecting',ssid:new URLSearchParams(options.body).get('ssid'),elapsed:1}}if(url==='/api/scan')scanning=false;return{ok:true,json:async()=>url==='/api/status'?state:url==='/api/networks'?{scanning,networks:list}:{message:'Connecting'}}}});
 vm.runInContext(script,context);
 async function flush(){await new Promise(setImmediate);await new Promise(setImmediate)}
 async function fire(ms){for(const [id,timer] of [...timers])if(timer.ms===ms){timers.delete(id);await timer.fn()}await flush()}
 return{elements,calls,timers,flush,fire,state:v=>state=v,list:v=>list=v,reject:v=>reject=v,select:async value=>{elements.network.value=value;elements.network.onchange();await flush()},submit:async()=>{await elements.form.onsubmit({preventDefault(){}});await flush()}};
}
(async()=>{
 let f=fixture();await f.flush();let e=f.elements;assert.equal(e.network.children.length,6);assert.equal(e.network.children[3].textContent,'<img src=x onerror=alert(1)>');assert(e.network.children[4].disabled);assert(e.connect.disabled);
 await f.select('0');e.password.value='dummy&+password';assert(!e.connect.disabled);await f.submit();let body=new URLSearchParams(f.calls.find(c=>c.url==='/api/connect').options.body);assert.equal(body.get('ssid'),'Home & Cafe');assert.equal(body.get('password'),'dummy&+password');assert(e.scan.disabled&&e.connect.disabled);
 f.state({state:'failed',message:'Wrong password'});await f.fire(1000);assert.equal(e.status.textContent,'Wrong password');assert(!e.connect.disabled);
 await f.select('1');assert(e.passarea.classes.has('hidden'));e.password.value='stale';await f.submit();body=new URLSearchParams(f.calls.filter(c=>c.url==='/api/connect').at(-1).options.body);assert.equal(body.get('password'),'');
 f.state({state:'connected',ip:'192.168.1.10'});await f.fire(1000);assert(e.status.textContent.includes('Pair ESP Lyric Speaker'));assert.equal(e.connect.textContent,'Connected');assert.equal([...f.timers.values()].filter(t=>t.ms===1000).length,0);
 f=fixture();await f.flush();e=f.elements;await f.select('manual');e.ssid.value='Hidden & Cafe';e.ssid.oninput();e.password.value='';await f.submit();body=new URLSearchParams(f.calls.find(c=>c.url==='/api/connect').options.body);assert.equal(body.get('ssid'),'Hidden & Cafe');assert.equal(body.get('password'),'');
 f=fixture();await f.flush();e=f.elements;await f.select('0');e.password.value='test-only';f.reject(true);await f.submit();assert.equal(e.status.dataset.kind,'error');assert(!e.connect.disabled);
 console.log('Actual portal JS: English/simple form, safe SSID text, scan selection, manual/open/secure network, escaped credentials, busy/failure/retry/connected states and stopped polling: PASS');
})().catch(error=>{console.error(error);process.exitCode=1});
