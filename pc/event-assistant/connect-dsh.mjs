// Register read-only board tools; retain the existing QQ access boundary.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import os from 'node:os';

const root=path.dirname(fileURLToPath(import.meta.url));
const bridge=process.env.QQ_BRIDGE_DIR || path.resolve(root,'../qq-bridge');
const require=createRequire(bridge+'/package.json');
const yaml=require('js-yaml');
const profile=process.env.DSH_PROFILE || path.join(os.homedir(),'.dsh/profiles/web/cordis.patch.yml');
const python=spawnSync(process.env.CARE_PYTHON || 'python',['-c','import sys;print(sys.executable)'],{encoding:'utf8'}).stdout?.trim();
if(!python||!fs.existsSync(python))throw Error('Python not found');
const backup=path.join(root,'integration-backup',new Date().toISOString().replace(/[:.]/g,'-'));
const changes=new Map();
const prompt=`      【板卡事件问答】
      - 询问老人今天的活动、去过哪些房间或停留情况时，必须先调用 mcp__board-events__board_activity_summary，日期默认 today；只按工具返回的真实采样记录回答。
      - 默认只答一两句，通常不超过60个汉字；先给结论，不复述问题、不汇报操作、不列长清单。追问依据时再给时间点和证据。
      - available=false 时明确说没有这一天的有效活动记录；记录不完整时说“已采集时段内”。未记录到某房间不能说全天没有去过，不推断食欲、疾病或受伤；建议只限于联系关怀、查看现场等。
      - “现在/温度/湿度/传感器”必须调用 mcp__board-events__board_current_status；离线时回答“板卡还没接通，暂时查不到实时数据”。示例末帧不能称为现场现状。
      - 统计针对选定主目标，目标ID不是人员身份。不能将不同ID或多人记录都归为同一个老人。报警经过使用 mcp__board-events__board_event_context；工具报错时不使用模拟样例替代真实记录。
      - 这三个只读工具只提供固定板卡状态与受限活动汇总，不提供任意本地文件、命令或网址访问。

`;
for(const preset of ['qq-chat','qq-chat-v2']){
  const file=path.join(bridge,'dsh/agent-presets',preset,'agent.cordis.yml');
  const old=fs.readFileSync(file,'utf8');
  if(old.includes('【板卡事件问答】')){
    const start=old.indexOf('      【板卡事件问答】');
    const end=old.indexOf('      【QQ',start);
    if(start<0||end<0)throw Error('Board prompt boundary missing');
    const next=old.slice(0,start)+prompt+old.slice(end);
    yaml.load(next);if(next!==old)changes.set(file,next);
  } else {
    const marker='      【QQ'; const i=old.indexOf(marker);
    if(i<0)throw Error('Persona anchor missing');
    const next=old.slice(0,i)+prompt+old.slice(i);
    yaml.load(next);changes.set(file,next);
  }
}
const guard=path.join(bridge,'plugins/qq-agent-presets/qq-tool-restrict.mjs');
const guardText=fs.readFileSync(guard,'utf8');
if(!guardText.includes("'mcp__board-events__board_activity_summary'")){
  const anchor='const SAFE_EXACT = new Set([\n';
  if(!guardText.includes(anchor))throw Error('Guard anchor missing');
  const names=['board_activity_summary','board_current_status','board_event_context'];
  const lines=names.filter(n=>!guardText.includes(`'mcp__board-events__${n}'`)).map(n=>`  'mcp__board-events__${n}',\n`).join('');
  changes.set(guard,guardText.replace(anchor,anchor+lines));
}
const patch=fs.readFileSync(profile,'utf8');
{
  const entry=[{insert:[{id:'mcp-board-events',name:'@deepseek-ai/dsh-mcp-client',config:{serverName:'board-events',transport:'stdio',command:python,args:[path.join(root,'assistant.py'),'--mcp','--live','--activity-db',path.join(root,'data/activity.sqlite3')]}}]}];
  const marker='\n# Offline board evidence tools (event-assistant-demo)';
  const liveMarker='\n# Read-only board evidence tools (event-assistant-demo)';
  const start=patch.includes(marker)?patch.indexOf(marker):patch.indexOf(liveMarker);
  if(patch.includes('id: mcp-board-events')&&start<0)throw Error('Unknown board MCP registration; preserve profile');
  // This script owns only its trailing registration block, never other profile rows.
  if(start>=0){const owned=yaml.load(patch.slice(start));if(owned.length!==1||owned[0].insert?.length!==1||owned[0].insert[0].id!=='mcp-board-events')throw Error('Profile contains subsequent rows; preserve profile');}
  const next=(start>=0?patch.slice(0,start):patch).trimEnd()+liveMarker+'\n'+yaml.dump(entry,{lineWidth:-1});
  yaml.load(next);changes.set(profile,next);
}
const generated=['qq-chat','qq-chat-v2'].map(p=>path.join(bridge,'plugins/qq-agent-presets/presets',p+'.patch.yml'));
fs.mkdirSync(backup,{recursive:true});
const manifest=[];
for(const file of new Set([...changes.keys(),...generated])){
  const saved=path.join(backup,String(manifest.length)+'.bak');fs.copyFileSync(file,saved);manifest.push({file,saved});
}
fs.writeFileSync(path.join(backup,'manifest.json'),JSON.stringify(manifest,null,2));
try{
  // Write the profile last so a live loader observes complete preset sources.
  for(const [file,text] of changes)if(file!==profile)fs.writeFileSync(file,text);
  const built=spawnSync(process.execPath,['scripts/build-agent-preset-patches.mjs'],{cwd:bridge,encoding:'utf8'});
  if(built.status!==0)throw Error('Preset generation failed');
  if(changes.has(profile))fs.writeFileSync(profile,changes.get(profile));
}catch(error){for(const row of manifest)fs.copyFileSync(row.saved,row.file);throw error;}
console.log(JSON.stringify({configured:true,mode:'live read-only; activity recorder required',changed:changes.size,backup},null,2));
