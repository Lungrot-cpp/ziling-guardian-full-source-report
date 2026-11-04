const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const page=fs.readFileSync('software/src/net/care.html','utf8');
const script=[...page.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(match=>match[1]).join('\n');
const elements=new Map(),posts=[],draws=[];let fail=false,now=100000;
class Clock extends Date{static now(){return now}}
const ctx={clearRect(){},drawImage(){},fillRect(x,y,w,h){draws.push({x,y,w,h,color:this.fillStyle})},strokeRect(){},beginPath(){},moveTo(){},lineTo(){},stroke(){},arc(){},fill(){},fillText(){}};
function el(id){if(!elements.has(id))elements.set(id,{value:'',checked:false,disabled:false,hidden:false,textContent:'',naturalWidth:0,children:[],classList:{toggle(){},remove(){}},append(...x){this.children.push(...x)},click(){},getContext(){return ctx},getBoundingClientRect(){return {left:0,top:0,width:960,height:540}}});return elements.get(id)}
const fixture={hardware:true,calibrated:true,valid:true,track:1,x:.2,y:.2,age_ms:100,room:0,wall:0,risk:0,armed:false,door_state:0,remaining_ms:0,countdown_ms:0,alarm:false,rooms:[{rect:[0,0,.5,.5],lit:true},{rect:[.5,0,1,.5],lit:false},{rect:[0,.5,.5,1],lit:false},{rect:[.5,.5,1,1],lit:false}],config:{brightness:20,leds_per_room:7,rotation:0,matrix_rgb:0,strip_rgb:0,serpentine:1,neutral_us:1500,open_us:1650,close_us:1350,run_ms:400,corners:[0,0,1,0,1,1,0,1]}};
fixture.config.brightness=3;fixture.trail_step_ms=3000;fixture.trail_ttl_ms=9000;fixture.trail_ages=Array(256).fill(-1);fixture.trail_ages[1]=0;
const context=vm.createContext({location:{search:'',origin:'http://192.168.0.2'},addEventListener(){},document:{hidden:false,documentElement:{classList:{toggle(){}}},getElementById:el,createElement:()=>el('created'+elements.size),querySelectorAll:()=>[el('room0'),el('room1'),el('room2'),el('room3')]},URL,URLSearchParams,AbortController,AbortSignal,Date:Clock,Blob,setTimeout(){return 1},clearTimeout(){},console,
 fetch:async(path,options)=>{if(fail)throw Error('offline');if(options?.method==='POST'){posts.push({path,data:new URLSearchParams(options.body)});return {ok:true,json:async()=>({ok:true})}}if(path==='/api/care')return {ok:true,json:async()=>fixture};if(path==='/api/replay')return {ok:true,json:async()=>({ready:false,buffered:2,count:0,frames:[]})};return {ok:false,status:503}}});
(async()=>{
 vm.runInContext(script,context);await new Promise(r=>setImmediate(r));
 assert.equal(el('position').textContent,'厨房');assert.equal(el('open').disabled,true);
 assert.equal(el('lighting').textContent,'照明：上墙');
 fixture.wall=3;await vm.runInContext('update()',context);assert.equal(el('lighting').textContent,'照明：左墙');
 function trailDraw(){draws.length=0;vm.runInContext('drawMap()',context);return draws.filter(d=>d.x===42&&d.y===10&&d.w===12&&d.h===12)}
 assert.equal(trailDraw()[0].color,'#62c78c');
 now=102999;assert.equal(trailDraw()[0].color,'#62c78c');
 now=103000;assert.equal(trailDraw()[0].color,'#519bd5');
 now=106000;assert.equal(trailDraw()[0].color,'#e67364');
 now=109000;assert.equal(trailDraw().length,0);
 fixture.trail_ages[1]=4500;await vm.runInContext('update()',context);
 assert.equal(trailDraw()[0].color,'#519bd5','Reload must preserve board-side trail age');
 el('source').naturalWidth=320;
 for(const [clientX,clientY] of [[96,54],[864,54],[864,486],[96,486]])el('camera').onclick({clientX,clientY});
 assert.equal(el('savePoints').disabled,false);await el('savePoints').onclick();
 assert.equal(posts.at(-1).path,'/api/care/config');assert.equal(posts.at(-1).data.get('p0'),'0.1000000');
 await el('clearTrail').onclick();assert.equal(posts.at(-1).data.get('cmd'),'clear_trail');
 await el('stop').onclick();assert.equal(posts.at(-1).data.get('cmd'),'stop');
 assert.equal(el('leds_per_room').value,7);
 fixture.alarm=true;await vm.runInContext('update()',context);
 assert.equal(el('lighting').textContent,'照明：四面墙红色闪烁');
 assert.match(el('rescue').textContent,/开门联动.*全部照明灯红闪/);
 assert.doesNotMatch(el('rescue').textContent,/秒后/);
 fail=true;await vm.runInContext('update()',context);
 assert.match(el('position').textContent,/连接中断/);assert.equal(el('open').disabled,true);assert.equal(vm.runInContext('connectedAt',context),0);
 assert.match(el('lighting').textContent,/状态未知/);
 console.log('CARE_FRONTEND_TEST_PASS: board-owned trail ages; green/blue/red/expiry without polling; reload preserves history age; configuration bootstrap; four corner calibration; stop/clear; offline movement guards');
})().catch(e=>{console.error(e);process.exitCode=1});
