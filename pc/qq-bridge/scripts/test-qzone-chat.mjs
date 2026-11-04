import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { qzoneChat } from '../src/qzone-chat.js';
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'qq-qzone-test-'));
const stateFile = path.join(dir, 'state.json');
const calls = [];
const deps = { owner: '123456789', stateFile,
  authorize: async (_key, token) => { assert.equal(token, 'valid'); },
  call: async (action, params) => {
    calls.push({ action, params });
    if (action === 'get_qzone_msg_list') return { total: 1, msglist: [{ tid: 'post', content: '周末去散步', time: 1, comment_num: 0 }] };
    if (action === 'get_qzone_msg_detail') { assert.equal(params.tid, 'post'); return { tid: 'post' }; }
    return { tid: 'new-post' };
  }
};
const base = { key: 'private:123456789', token: 'valid' };
try {
  await assert.rejects(qzoneChat({ ...base, key: 'private:123', action: 'owner' }, deps));
  await assert.rejects(qzoneChat({ ...base, token: '', action: 'owner' }, deps));
  assert.equal(calls.length, 0);
  assert.equal((await qzoneChat({ ...base, action: 'owner' }, deps)).posts[0].tid, 'post');
  await assert.rejects(qzoneChat({ ...base, action: 'comment', content: '你好' }, deps));
  const posts = await Promise.allSettled([1, 2].map(() => qzoneChat({ ...base, action: 'publish', content: '今天阳光好好喵。' }, deps)));
  assert.equal(posts.filter(p => p.status === 'fulfilled').length, 1);
  assert.equal(calls.filter(c => c.action === 'send_qzone_msg').length, 1);
  assert.equal(calls.find(c => c.action === 'send_qzone_msg').params.ugc_right, 4);
  await qzoneChat({ ...base, action: 'like', tid: 'post' }, deps);
  assert.equal((await qzoneChat({ ...base, action: 'like', tid: 'post' }, deps)).alreadyDone, true);
  assert.equal(calls.filter(c => c.action === 'like_qzone').length, 1);
  await qzoneChat({ ...base, action: 'comment', tid: 'post', content: '听起来好舒服呀。' }, deps);
  assert.equal(calls.find(c => c.action === 'comment_qzone').params.target_uin, 123456789);
  const saved = JSON.parse(fs.readFileSync(stateFile, 'utf8'));
  fs.writeFileSync(stateFile, JSON.stringify({ ...saved, day: '2000-01-01' }));
  assert.equal((await qzoneChat({ ...base, action: 'like', tid: 'post' }, deps)).alreadyDone, true);
  assert.equal(calls.filter(c => c.action === 'like_qzone').length, 1);
  console.log('QZONE_CHECK_OK: owner/token, real handles, visibility, concurrent publish, duplicate likes, comments');
} finally {
  if (fs.existsSync(stateFile)) fs.unlinkSync(stateFile);
  fs.rmdirSync(dir);
}
