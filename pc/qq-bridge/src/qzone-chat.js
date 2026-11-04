import fs from 'node:fs';
import { SENSITIVE_RE } from './sensitive.js';

let queue = Promise.resolve();
export function qzoneChat(input, dependencies) {
  const task = queue.then(() => runQzone(input, dependencies));
  queue = task.catch(() => {});
  return task;
}

async function runQzone(input, { owner, stateFile, authorize, call }) {
  const { key, token, action, tid, content } = input;
  if (!owner || key !== `private:${owner}` || !token) throw new Error('空间工具仅限主人的已授权私聊');
  await authorize(key, token);
  if (!['mine', 'owner', 'publish', 'comment', 'like'].includes(action)) throw new Error('未知空间操作');
  if (['publish', 'comment'].includes(action)) {
    if (!content?.trim() || content.length > 300 || SENSITIVE_RE.test(content)) throw new Error('正文为空、过长或含敏感信息');
  }
  const day = new Intl.DateTimeFormat('en-CA', { timeZone: 'Asia/Shanghai' }).format(new Date());
  let state = { day, published: false, done: [], actions: 0 };
  if (fs.existsSync(stateFile)) {
    const saved = JSON.parse(fs.readFileSync(stateFile, 'utf8'));
    state = { ...saved, actions: saved.actions ?? saved.done.length };
    if (saved.day !== day) state = { ...state, day, published: false, actions: 0 };
  }
  if (action === 'mine' || action === 'owner') {
    const params = { pos: 0, num: 3, ...(action === 'owner' ? { target_uin: Number(owner) } : {}) };
    const data = await call('get_qzone_msg_list', params);
    return { total: data.total, posts: (data.msglist ?? []).map(p => ({ tid: p.tid, content: p.content.slice(0, 400), time: p.time, comments: p.comment_num })) };
  }
  if (action === 'publish' && state.published) throw new Error('今天已发过说说，每天最多一条');
  if (action !== 'publish') {
    if (!tid || tid.length > 128) throw new Error('需要先查看说说取得 tid');
    const detail = await call('get_qzone_msg_detail', { target_uin: Number(owner), tid, pos: 0, num: 1 });
    if (!detail) throw new Error('无法核实这条说说');
    if (state.done.includes(`${action}:${tid}`)) return { alreadyDone: true };
  }
  if (state.actions >= 10) throw new Error('今天的空间互动已达上限');
  const result = action === 'publish'
    ? await call('send_qzone_msg', { content: content.trim(), ugc_right: 4 })
    : await call(action === 'like' ? 'like_qzone' : 'comment_qzone', { target_uin: Number(owner), tid, ...(action === 'comment' ? { content: content.trim() } : {}) });
  if (action === 'publish') state.published = true;
  state.done.push(`${action}:${tid ?? result?.tid}`);
  state.actions += 1;
  fs.writeFileSync(`${stateFile}.tmp`, JSON.stringify(state));
  fs.renameSync(`${stateFile}.tmp`, stateFile);
  return { ok: true, action, tid: result?.tid ?? tid };
}
