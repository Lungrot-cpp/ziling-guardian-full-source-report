const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const script=fs.readFileSync(__dirname+'/../software/src/net/privacy.js','utf8');
const handlers={},draws=[],small={width:0,height:0,getContext:()=>({clearRect(){},drawImage(...args){draws.push(args)}})};
const context=vm.createContext({URLSearchParams,location:{search:''},Event:class{constructor(type){this.type=type}},document:{
  createElement:()=>small,getElementById:()=>null,querySelectorAll:()=>[],addEventListener:(type,fn)=>handlers[type]=fn},
  dispatchEvent(){},setInterval(){}});
context.parent=context;vm.runInContext(script,context);
function press(extra={}){let prevented=false;handlers.keydown({key:'y',target:{},preventDefault(){prevented=true},...extra});return prevented;}
assert.equal(context.vfPrivacy.enabled,false);
assert(press());assert.equal(context.vfPrivacy.enabled,true);
for(const extra of [{repeat:true},{isComposing:true},{ctrlKey:true},{altKey:true},{metaKey:true},{target:{isContentEditable:true}},{target:{closest:()=>({})}}]){
  assert.equal(press(extra),false);assert.equal(context.vfPrivacy.enabled,true);
}
let smoothing=true,restored=false,targetSource;
const ctx={save(){},restore(){restored=true},set imageSmoothingEnabled(value){smoothing=value},drawImage(source){targetSource=source}};
const realImage={};context.vfPrivacy.draw(ctx,realImage,0,0,960,540);
assert.equal(draws[0][0],realImage);assert.equal(draws[0][3],32);assert.equal(draws[0][4],18);
assert.equal(targetSource,small);assert.equal(smoothing,false);assert(restored);
assert(press({key:'Y'}));assert.equal(context.vfPrivacy.enabled,false);
context.vfPrivacy.draw(ctx,realImage,0,0,960,540);assert.equal(targetSource,realImage);
console.log('PRIVACY_FRONTEND_PASS: Y toggle, held/composing/modifier/editing guards, mosaic downsampling and clear restore; fixture only');
