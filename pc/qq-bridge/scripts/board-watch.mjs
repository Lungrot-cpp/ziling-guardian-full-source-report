import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { setTimeout as sleep } from 'node:timers/promises';
import net from 'node:net';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const stateFile = path.join(root, 'state/board-watch.json');
const read = (file) => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
function save(state) {
  fs.writeFileSync(stateFile + '.pending', JSON.stringify(state, null, 2));
  fs.renameSync(stateFile + '.pending', stateFile);
}

export function updateMonitor(previous, status, ledger, now) {
  const uint = (n) => Number.isInteger(n) && n >= 0 && n <= 0xffffffff;
  if (status.server !== 'riscv-lwip' || !uint(status.uptime_ms) || !uint(status.frame_id)
      || ![0, 1].includes(status.alarm_latched) || !Array.isArray(status.tracks)
      || !status.tracks.every((t) => uint(t.id) && [0, 1, 2].includes(t.state))
      || ledger.volatile !== true || !Array.isArray(ledger.events) || ledger.events.length > 16
      || !ledger.events.every((e) => uint(e.id) && uint(e.track) && uint(e.uptime_ms)
        && uint(e.frame) && [0, 1, 2].includes(e.state))) throw new Error('板卡接口格式不匹配');
  const maxId = Math.max(0, ...ledger.events.map((e) => e.id));
  // shortcut: firmware has no boot UUID; a reboot whose uptime and event IDs both catch up can be missed.
  const reset = previous.uptimeMs == null || status.uptime_ms < previous.uptimeMs || maxId < previous.lastEventId;
  const s = { ...previous, pending: [...(previous.pending || [])], checkedAt: now, monitorPid: process.pid,
    board: 'http://192.168.0.2', uptimeMs: status.uptime_ms, frameId: status.frame_id, alarmLatched: status.alarm_latched };
  const progressed = previous.uptimeMs != null && status.uptime_ms !== previous.uptimeMs;
  s.progressAt = reset || progressed ? now : previous.progressAt;
  s.confirmed = reset ? false : previous.confirmed || progressed;
  s.health = s.confirmed && now - s.progressAt < 15000 ? 'monitoring' : 'waiting_for_fresh_data';
  const add = (id, text) => s.pending.push({ id, text, observedAt: new Date(now).toISOString() });
  if (reset) {
    s.generation = (previous.generation || 0) + 1;
    s.lastEventId = Math.max(0, ...ledger.events.filter((e) => e.uptime_ms <= status.uptime_ms).map((e) => e.id));
    if (status.alarm_latched || status.tracks.some((t) => t.state === 2)) {
      add(`${s.generation}:initial`, '【板卡报警】接入时已有报警记录，请查看现场。锁存报警不能确认当前姿态。');
    }
  } else {
    const pendingCount = s.pending.length;
    for (const e of [...ledger.events].sort((a, b) => a.id - b.id)) {
      // Status and event endpoints are separate snapshots; defer newer events until the next poll.
      if (e.id <= s.lastEventId || e.uptime_ms > status.uptime_ms) continue;
      if (e.state === 2) add(`${s.generation}:${e.id}`,
        '自动告警：检测到人员跌倒。疑似意外，请求紧急救援。');
      s.lastEventId = e.id;
    }
    if (status.alarm_latched && !previous.alarmLatched && s.pending.length === pendingCount) {
      add(`${s.generation}:latch:${status.uptime_ms}`, '【板卡报警】板卡出现锁存报警，请查看现场。暂无对应跌倒事件，不能确认姿态。');
    }
  }
  return s;
}

export async function sendNotice(cfg, text, fetcher = fetch) {
  const owner = Number(cfg.ownerQQ);
  if (!Number.isSafeInteger(owner) || owner <= 0 || !(cfg.allow?.private || []).some((id) => String(id) === String(owner))) {
    throw new Error('提醒接收人必须是已配置的私聊管理员');
  }
  const response = await fetcher(`${cfg.snowluma.httpUrl.replace(/\/+$/, '')}/send_private_msg`, {
    method: 'POST', headers: { 'content-type': 'application/json', authorization: `Bearer ${cfg.snowluma.accessToken}` },
    body: JSON.stringify({ user_id: owner, message: [{ type: 'text', data: { text } }] }),
    signal: AbortSignal.timeout(15000),
  });
  const body = await response.json();
  if (!response.ok || body.status !== 'ok' || body.retcode !== 0 || body.data?.message_id == null) {
    throw new Error('QQ未确认提醒发送成功');
  }
  return body.data.message_id;
}

async function board(endpoint) {
  const response = await fetch(`http://192.168.0.2${endpoint}`, { signal: AbortSignal.timeout(3000) });
  if (!response.ok) throw new Error('板卡读取失败');
  const text = await response.text();
  if (text.length > 100000) throw new Error('板卡响应超长');
  return JSON.parse(text);
}

async function acquire() {
  // Windows releases this named-pipe lock even after a crash or reboot; PID reuse cannot block startup.
  const lock = net.createServer((socket) => socket.destroy());
  return new Promise((resolve, reject) => {
    lock.once('error', (e) => e.code === 'EADDRINUSE' ? resolve(false) : reject(e));
    lock.listen('\\\\.\\pipe\\qq-bridge-board-watch-xcy', () => resolve(true));
  });
}

async function main() {
  if (process.argv.includes('--test-notification')) {
    const id = await sendNotice(read(path.join(root, 'config.json')),
      '【模拟测试·非现场报警】主动提醒已接通。板卡触发报警后，会在这里提醒你；当前板卡尚未连接。');
    console.log(JSON.stringify({ testNotification: 'accepted_by_QQ', messageId: id }));
    return;
  }
  if (!await acquire()) return;
  let s = {};
  try { s = read(stateFile); } catch (e) { if (e.code !== 'ENOENT') throw e; }
  // Reconfirm progress after every process restart before sending queued alerts.
  s.confirmed = false;
  s.progressAt = Date.now();
  while (true) {
    try {
      const status = await board('/api/status');
      const ledger = await board('/api/events');
      s = updateMonitor(s, status, ledger, Date.now());
      delete s.error;
      save(s);
      if (s.health === 'monitoring' && s.pending.length && Date.now() >= (s.retryAt || 0)) {
        try {
          const item = s.pending[0];
          const id = await sendNotice(read(path.join(root, 'config.json')), item.text);
          s.lastSent = { ...item, messageId: id, sentAt: new Date().toISOString() };
          s.pending.shift();
          delete s.retryAt;
          delete s.deliveryError;
        } catch {
          s.deliveryError = 'QQ发送未确认，30秒后重试';
          s.retryAt = Date.now() + 30000;
        }
      }
    } catch {
      s.health = 'board_unavailable';
      s.error = '板卡未连接或接口不匹配；未生成报警';
      s.confirmed = false;
    }
    s.checkedAt = Date.now();
    s.monitorPid = process.pid;
    save(s);
    await sleep(2000);
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch(() => { console.error('板卡监测启动失败，请检查状态文件与本机配置'); process.exitCode = 1; });
}
