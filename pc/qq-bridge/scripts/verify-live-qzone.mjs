import fs from 'node:fs';
import assert from 'node:assert/strict';
import {Client} from '@modelcontextprotocol/sdk/client/index.js';
import {StdioClientTransport} from '@modelcontextprotocol/sdk/client/stdio.js';
const root=process.cwd();
const key='private:123456789';
const token=JSON.parse(fs.readFileSync('state/social-v2.json','utf8')).conversations[key].agentToken;
assert(token);
const client=new Client({name:'qzone-local-check',version:'1.0'});
const transport=new StdioClientTransport({command:process.execPath,args:['src/mcp-snowluma-safe.js'],cwd:root,stderr:'ignore'});
try{
  await client.connect(transport);
  assert((await client.listTools()).tools.some(t=>t.name==='qq_qzone'));
  const bad=await client.callTool({name:'qq_qzone',arguments:{key,token:'invalid',action:'owner'}});
  assert.equal(bad.isError,true);
  const result=await client.callTool({name:'qq_qzone',arguments:{key,token,action:'owner'}});
  assert(!result.isError, result.content[0]?.text);
  const data=JSON.parse(result.content[0].text);
  assert(data.posts.length>0);
  console.log('LIVE_QZONE_READ_OK',data.posts.length,'posts; invalid token rejected');
  if(process.argv.includes('--like')) {
    const liked=await client.callTool({name:'qq_qzone',arguments:{key,token,action:'like',tid:data.posts[0].tid}});
    assert(!liked.isError,liked.content[0]?.text);
    assert.equal(JSON.parse(liked.content[0].text).ok,true);
    console.log('LIVE_QZONE_LIKE_OK');
  }
}finally{await client.close();}
