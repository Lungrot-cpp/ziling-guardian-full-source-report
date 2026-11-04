import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { synthesizeVoice, resolveTtsVoiceId, TTS_VOICES } from '../src/voice-tts.js';

const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'qq-voice-tts-'));
try {
  for (const text of ['', '   ', '字'.repeat(301), null]) {
    await assert.rejects(synthesizeVoice(text, dir), /1–300/);
  }
  assert.equal(fs.readdirSync(dir).length, 0);
  assert.equal(resolveTtsVoiceId(), 'yujie');
  for (const voice of TTS_VOICES) assert.equal(resolveTtsVoiceId(voice.id), voice.id);
  for (const id of ['../voice-reference.wav', '__proto__', '', null, {}]) {
    assert.throws(() => resolveTtsVoiceId(id), /音色/);
    await assert.rejects(synthesizeVoice('你好', dir, id), /音色/);
  }
  const pending = synthesizeVoice('你好，今天过得怎么样？引号和符号只会作为文本：" $();', dir);
  await assert.rejects(synthesizeVoice('同时请求', dir), /上一条语音/);
  const file = await pending;
  const audio = fs.readFileSync(file);
  assert.equal(path.dirname(file), dir);
  assert.equal(audio.toString('ascii', 0, 4), 'RIFF');
  assert.equal(audio.toString('ascii', 8, 12), 'WAVE');
  assert.ok(audio.length > 1000);
  console.log('实时中文语音：输入校验、并发保护、命令字符隔离和参考音色 WAV 合成通过');
} finally {
  fs.rmSync(dir, { recursive: true, force: true });
}
