import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { bridgeHarness } from './audit-bridge-harness.mjs';
import * as roleCard from '../src/role-card.js';
import { DEFAULT_TTS_VOICE, TTS_VOICES } from '../src/voice-tts.js';

let hour = 12;
let now = Date.now();
class Clock extends Date {
  static now() { return now; }
  getHours() { return hour; }
}
const h = await bridgeHarness({
  config: { socialV2: { proactive: { enabled: true, checkIntervalMinMs: 60000, checkIntervalMaxMs: 60000, idleThresholdMs: 60000, probability: 1 } } },
  files: { 'state/current-role.json': { role: '测试', mode: 'active' }, 'roles/测试.md': '温柔喜欢对方，先接住失落。' },
  globals: { ...roleCard, DEFAULT_TTS_VOICE, TTS_VOICES, Date: Clock,
    setTimeout: (fn, ms) => ({ fn, ms, unref() {} }), clearTimeout() {} },
});
try {
  h.setMode('reserved2');
  h.snowlumaHealth.connected = true;
  h.snowlumaHealth.accountOnline = true;
  const key = 'private:123';
  const st = h.getSocialV2State(key);
  const prompt = h.buildWakePromptV2(key, 'proactiveCheck');
  assert.match(prompt, /温柔喜欢对方/);
  assert.match(prompt, /不为找话题联网/);
  assert.doesNotMatch(prompt, /可以装傻、已读乱回|web_search/);
  fs.writeFileSync(path.join(h.temp, 'roles/测试.md'), '最新角色：温柔、害羞、主动表达喜欢。');
  assert.match(h.buildWakePromptV2(key, 'private'), /最新角色/);
  st.lastIncomingAt = now - 7200000;
  st.lastProactiveAt = now - 3600000;
  st.lastAiReplyAt = now - 3500000;
  st.proactiveTimer.fn();
  await new Promise(setImmediate);
  assert.equal(h.calls.prompts.length, 0, '无人回应时不发起模型调用');
  hour = 23;
  st.lastIncomingAt = now - 1800000;
  st.proactiveTimer.fn();
  await new Promise(setImmediate);
  assert.equal(h.calls.prompts.length, 0, '夜间不主动唤醒模型');
  hour = 12;
  h.snowlumaHealth.connected = false;
  st.proactiveTimer.fn();
  await new Promise(setImmediate);
  assert.equal(h.calls.prompts.length, 0, 'QQ 离线时不花模型费用');
  h.snowlumaHealth.connected = true;
  st.lastIncomingAt = now - 7200000;
  st.lastAiReplyAt = now - 7200000;
  st.proactiveTimer.fn();
  await new Promise(setImmediate);
  assert.equal(h.calls.prompts.length, 1, '上次只有唤醒、发送失败时不能一直等用户回复');
  assert.equal(st.lastProactiveAt, now);
  const saved = JSON.parse(fs.readFileSync(path.join(h.temp, 'state/social-v2.json')));
  assert.equal(saved.conversations[key].lastProactiveAt, now, '重启后仍记得已主动联系');
  assert.equal(h.calls.sent.length, 0, '所有验证仅使用假的 QQ 和模型');
  console.log('PASS private persona refresh and proactive calls stop while unanswered or at night');
} finally { await h.close(); }
