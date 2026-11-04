import fs from 'node:fs';
import path from 'node:path';
import { randomUUID } from 'node:crypto';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = fileURLToPath(new URL('../', import.meta.url));
export const DEFAULT_TTS_VOICE = 'yujie';
export const TTS_VOICES = [
  { id: 'yujie', name: '御姐音', description: '成熟沉稳、音调偏低的女声' },
  { id: 'original', name: '唱段参考音', description: '之前用《娃娃脸》唱段生成的音色' }
];
export function resolveTtsVoiceId(id = DEFAULT_TTS_VOICE) {
  if (!TTS_VOICES.some((voice) => voice.id === id)) throw new Error('音色只能选择 yujie（御姐音）或 original（唱段参考音）');
  return id;
}
let generating = false;

export async function synthesizeVoice(text, voiceDir, voiceId = DEFAULT_TTS_VOICE) {
  if (typeof text !== 'string' || !text.trim() || [...text].length > 300) {
    throw new Error('语音文本必须为 1–300 个字符');
  }
  resolveTtsVoiceId(voiceId);
  if (generating) throw new Error('正在生成上一条语音，请稍后再试');
  generating = true;
  const file = path.join(voiceDir, `.tts-${randomUUID()}.wav`);
  try {
    fs.mkdirSync(voiceDir, { recursive: true });
    await new Promise((resolve, reject) => {
      // 固定脚本从标准输入读取 JSON，文本和输出路径不参与命令拼接。
      const child = spawn(path.join(ROOT, '.venv-tts/Scripts/python.exe'), [path.join(ROOT, 'scripts/synthesize-reference-voice.py')], {
        windowsHide: true, stdio: ['pipe', 'ignore', 'inherit'],
        env: { ...process.env, MKL_THREADING_LAYER: 'SEQUENTIAL', HF_HUB_OFFLINE: '1', TRANSFORMERS_OFFLINE: '1', PYTHONUTF8: '1' }
      });
      const timer = setTimeout(() => child.kill(), 240000);
      child.once('error', () => { clearTimeout(timer); reject(new Error('无法启动本地中文语音合成')); });
      child.once('close', (code) => {
        clearTimeout(timer);
        if (code === 0) resolve();
        else reject(new Error('参考音色合成失败或超时，请检查显卡可用显存和本地语音环境'));
      });
      child.stdin.on('error', () => {});
      child.stdin.end(JSON.stringify({ text: text.trim(), path: file, voiceId }));
    });
    if (fs.statSync(file).size <= 44) throw new Error('语音合成未生成有效音频');
    return file;
  } catch (error) {
    fs.rmSync(file, { force: true });
    throw error;
  } finally {
    generating = false;
  }
}
