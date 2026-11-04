import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';
import { normalizeUsage, priceUsage, resolvePriceTable } from '../src/model-prices.js';
import { NodeApiClient } from '../src/dsh-client.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const state = path.join(root, 'state');
const home = process.env.DSH_HOME || path.join(os.homedir(), '.dsh');
const sessions = path.join(home, 'sessions/--D-qqbrge-Derpyu520-qq-bridge-abda6bf-state-agents--');
const statusFile = path.join(state, 'spend-watch.json');
const logFile = path.join(state, 'spend-watch.log');
const dayOf = (time) => new Date(time + 8 * 3600000).toISOString().slice(0, 10);
const read = (file) => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const write = (file, data) => {
  fs.writeFileSync(file + '.pending', data, { encoding: 'utf8', mode: 0o600 });
  fs.renameSync(file + '.pending', file);
};
const log = (message) => fs.appendFileSync(logFile, `${new Date().toISOString()} ${message}\n`);

export function audit(input, now, table) {
  const day = dayOf(now);
  const rows = input.sessions.flatMap((session) => session.requests).filter((r) => dayOf(r.time) === day)
    .map((r) => {
      const buckets = normalizeUsage(r.usage);
      const price = priceUsage(buckets, { model: r.model, at: r.time, table });
      const purpose = r.tools.includes('compress') ? '压缩上下文'
        : r.tools.some((t) => /qq_send_voice/.test(t)) ? '发送歌曲或语音'
        : r.tools.some((t) => /qq_list_voices/.test(t)) ? '查询音频库'
        : r.tools.some((t) => /qq_send|qq_reply/.test(t)) ? '发送聊天消息'
        : r.tools.some((t) => /wait_for/.test(t)) ? '等待消息'
        : r.tools.some((t) => /wake_config/.test(t)) ? '设置唤醒'
        : r.tools.some((t) => /status|unread/.test(t)) ? '检查消息或连接' : '聊天推理或收尾';
      return { ...r, usage: undefined, ...buckets, cost: price.cost.total, purpose,
        priceEstimated: price.rates.estimated };
    }).sort((a, b) => a.time - b.time);
  const total = rows.reduce((sum, r) => sum + r.cost, 0);
  const byPurpose = {};
  for (const r of rows) byPurpose[r.purpose] = (byPurpose[r.purpose] || 0) + r.cost;
  const loops = [];
  for (const session of input.sessions) {
    if (session.activeTurn == null || now - session.lastTime > 90000) continue;
    const counts = new Map();
    for (const f of session.failures) {
      if (f.turn === session.activeTurn && now - f.time < 120000) counts.set(f.tool, (counts.get(f.tool) || 0) + 1);
    }
    for (const [tool, count] of counts) if (count >= 3) loops.push({ sessionId: session.requests[0]?.sessionId,
      turn: session.activeTurn, tool, count });
  }
  return { day, total, byPurpose, rows, loops };
}

async function check() {
  const cfg = read(path.join(root, 'config.json'));
  const input = JSON.parse(execFileSync(path.resolve(root, '../singing/.venv/Scripts/python.exe'),
    [path.join(root, 'scripts/read-spend-events.py'), sessions],
    { encoding: 'utf8', windowsHide: true, timeout: 15000, maxBuffer: 20 * 1024 * 1024 }));
  if (!input.sessions.length) throw new Error('未读取到 QQ 会话账本');
  const now = Date.now();
  const report = audit(input, now, resolvePriceTable(cfg.pricing));
  let previous = {};
  try { previous = read(statusFile); } catch (error) { if (error.code !== 'ENOENT') throw error; }
  const handled = previous.day === report.day ? previous.handledLoops || [] : [];
  const api = new NodeApiClient(cfg.dsh.baseUrl, 10000);
  for (const loop of report.loops) {
    const id = `${loop.sessionId}:${loop.turn}`;
    if (handled.includes(id)) continue;
    // Only cancel this failing turn; never pause the bot or disable normal messages.
    const cancelled = await api.callUnary('session/cancel', { sessionId: loop.sessionId });
    if (!cancelled.result?.ok) throw new Error('取消异常回合失败');
    handled.push(id);
    log(`终止异常回合 ${id}：${loop.tool} 两分钟内失败 ${loop.count} 次`);
  }
  let account = null;
  try {
    const usage = read(path.join(home, '.dshw-usage.json'));
    if (usage.date === report.day) account = { amount: usage.todayUsage,
      source: '本机账户余额缓存，包含其他应用；不是逐笔官方对账',
      cachedAt: fs.statSync(path.join(home, '.dshw-usage.json')).mtime.toISOString() };
  } catch (error) { if (error.code !== 'ENOENT') throw error; }
  const watchedAmount = Math.max(report.total, Number(account?.amount) || 0);
  const alertId = watchedAmount >= 2 ? `${report.day}:over2:${Math.floor(watchedAmount * 2)}:${handled.length}` : null;
  const fields = ['time','sessionId','turn','step','retry','model','provider','purpose','tools','cacheMiss','cacheHit','output','cost'];
  const csvCell = (v) => '"' + String(v ?? '').replace(/^[=+\-@]/, "'$&").replaceAll('"', '""') + '"';
  const csv = '\uFEFF' + [fields.join(','), ...report.rows.map((r) => fields.map((key) => csvCell(
    key === 'time' ? new Date(r.time).toISOString() : key === 'tools' ? r.tools.join(';') : r[key])).join(','))].join('\n');
  const detailFile = path.join(state, `spend-${report.day}.csv`);
  write(detailFile, csv);
  write(statusFile, JSON.stringify({ day: report.day, checkedAt: new Date(now).toISOString(), monitorPid: process.pid,
    warningYuan: 2, pauseOnBudget: false, qqEstimatedYuan: report.total, requests: report.rows.length,
    byPurpose: report.byPurpose, account, alertId, handledLoops: handled, incompleteSessions: input.incomplete,
    detailFile, limitation: '按模型返回的用量估算，不含未返回用量的失败请求及其他应用逐笔明细' }, null, 2));
  if (alertId && alertId !== previous.alertId) log(`费用待排查：QQ 估算 ${report.total.toFixed(4)} 元，账户缓存 ${account?.amount ?? '未知'} 元；不暂停`);
}

async function main() {
  const lockFile = path.join(state, 'spend-watch.pid');
  if (!process.argv.includes('--once')) {
    try {
      const pid = Number(fs.readFileSync(lockFile, 'utf8'));
      process.kill(pid, 0);
      return;
    } catch (error) { if (!['ENOENT', 'ESRCH'].includes(error.code)) throw error; }
    fs.writeFileSync(lockFile, String(process.pid));
  }
  do {
    try { await check(); }
    catch (error) { log(`监控错误：${error.message.split('\n')[0]}`); if (process.argv.includes('--once')) throw error; }
    if (process.argv.includes('--once')) break;
    await sleep(15000);
  } while (true);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch(() => { process.exitCode = 1; });
}
