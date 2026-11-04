import assert from 'node:assert/strict';
import { bridgeHarness } from './audit-bridge-harness.mjs';
import { DEFAULT_TTS_VOICE, TTS_VOICES } from '../src/voice-tts.js';

const h = await bridgeHarness({ globals: { DEFAULT_TTS_VOICE, TTS_VOICES } });
try {
  h.setMode('reserved2');
  const key = 'private:123';
  const sid = await h.ensureSession(key);
  const st = h.getSocialV2State(key);
  st.activeTopics.push({ content: '保留的记忆' });
  st.ttsVoiceId = 'yujie';
  h.appendSocialV2Message(key, '用户', '继续聊天', '继续聊天', false, false, 'latest');
  st.bootstrapSent = true;
  const oldToken = st.agentToken;
  const end = (turn, message) => {
    h.trackTurnEvent(sid, { type: 'turn/start', data: { turn } });
    return h.trackTurnEvent(sid, { type: 'turn/end', data: { turn, reason: { kind: 'error', error: { message } } } });
  };
  assert.ok(!end(1, 'network timeout').contextRecovered);
  assert.equal(h.state.sessions[key], sid, '普通网络错误不清空会话');
  const result = end(2, 'context ~82074 tokens exceeds the model window 65536: preflight compression could not bring it under: no two-block span stayed under the 60,000-character source budget');
  assert.equal(result.contextRecovered, true);
  assert.equal(h.state.sessions[key], undefined, '压缩无法恢复的上下文必须退役');
  assert.equal(h.getSocialV2State(key), st, '保留社交状态对象');
  assert.equal(st.activeTopics[0].content, '保留的记忆');
  assert.equal(st.recentMessages.at(-1).text, '继续聊天');
  assert.equal(st.ttsVoiceId, 'yujie');
  assert.equal(st.bootstrapSent, false, '新会话重新加载角色与会话令牌');
  assert.notEqual(st.agentToken, oldToken);
  assert.equal(h.socialV2.paused, false);
  assert.equal(h.calls.prompts.length, 0, '恢复不得触发付费模型重试');
  await new Promise((resolve) => setImmediate(resolve));
  assert.ok(h.calls.cancelled.includes(sid));
  assert.ok(h.calls.archived.includes(sid), '旧会话归档，保留历史');
  const fresh = await h.ensureSession(key);
  assert.notEqual(fresh, sid);
  assert.ok(!end(2, 'exceeds the model window: preflight compression could not bring it under').contextRecovered);
  assert.equal(h.state.sessions[key], fresh, '重放旧错误不得停用新会话');
  console.log('PASS context recovery preserves memory, voice and recent chat without paid retries');
} finally {
  await h.close();
}
