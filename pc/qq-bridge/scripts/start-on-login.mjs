import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn, execFileSync } from 'node:child_process';
import { setTimeout as sleep } from 'node:timers/promises';
import assert from 'node:assert/strict';
import net from 'node:net';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const snowHome = path.resolve(root, '../snowluma');
const state = path.join(root, 'state');
const cfg = JSON.parse(fs.readFileSync(path.join(root, 'config.json'), 'utf8'));
const botUin = '3771755152';
const loginAccounts = [botUin, '123456789'];
const snowUrl = 'http://127.0.0.1:5099';
const bridgeUrl = 'http://127.0.0.1:3100';
const log = (message) => {
  fs.appendFileSync(path.join(state, 'autostart.log'), `${new Date().toISOString()} ${message}\n`);
  console.log(message);
};

async function request(url, headers = {}, body) {
  const response = await fetch(url, {
    method: body === undefined ? 'GET' : 'POST',
    headers: { ...headers, ...(body === undefined ? {} : { 'content-type': 'application/json' }) },
    body: body === undefined ? undefined : JSON.stringify(body),
    signal: AbortSignal.timeout(5000),
  });
  if (!response.ok) throw new Error(`本机接口返回 ${response.status}`);
  return response.json();
}

async function available(url) {
  try { return (await fetch(url, { signal: AbortSignal.timeout(2000) })).status < 500; }
  catch { return false; }
}

function launch(name, executable, args = [], cwd = root) {
  const child = spawn(executable, args, { cwd, windowsHide: true, detached: true, stdio: 'ignore' });
  child.once('error', () => log(`${name} 启动失败，请检查安装位置`));
  child.unref();
  log(`已启动 ${name}`);
}

export function planQQAccounts(list) {
  const accountOf = (item) => String(item.account?.uin ?? item.uin ?? '');
  return {
    bot: list.find((item) => accountOf(item) === botUin),
    missing: loginAccounts.filter((uin) => !list.some((item) => accountOf(item) === uin)),
  };
}

async function snowHeaders() {
  // CurrentUser DPAPI keeps the console password out of source, arguments and logs.
  const decrypt = "$s=ConvertTo-SecureString ([Console]::In.ReadToEnd().Trim()); $p=[Runtime.InteropServices.Marshal]::SecureStringToBSTR($s); try { [Console]::Out.Write([Runtime.InteropServices.Marshal]::PtrToStringBSTR($p)) } finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($p) }";
  let password = execFileSync(path.join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0/powershell.exe'), ['-NoProfile', '-NonInteractive', '-Command', decrypt], {
    input: fs.readFileSync(path.join(state, 'snowluma-autostart.credential'), 'utf8'),
    env: { ...process.env, PSModulePath: path.join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0/Modules') },
    encoding: 'utf8', windowsHide: true, timeout: 15000, stdio: ['pipe', 'pipe', 'ignore'],
  });
  const login = await request(`${snowUrl}/api/login`, {}, { password });
  password = '';
  if (!login.success || !login.token) throw new Error('SnowLuma 自动登录失败，请检查控制台密码是否已修改');
  return { 'x-snowluma-token': login.token };
}

export async function reconnectBot(list, attempt, now, load) {
  const bot = planQQAccounts(list).bot;
  if (!bot) return '等待机器人账号重新登录';
  if (bot.connected) return bot.loggedIn ? 'online' : '等待机器人账号完成登录或安全验证';
  if (bot.status === 'loading' || (attempt.pid === bot.pid && now - attempt.at < 30000)) {
    return '等待机器人连接';
  }
  assert(Number.isSafeInteger(bot.pid) && bot.pid > 0, '机器人进程号无效');
  // Retry the same process slowly; a newly logged-in process can load immediately.
  attempt.pid = bot.pid;
  attempt.at = now;
  const loaded = await load(bot.pid);
  if (!loaded.success) throw new Error('机器人自动注入未成功，稍后重试');
  return '等待机器人连接';
}

export async function acquireStartupLock(pipe = '\\\\.\\pipe\\qq-bridge-start-on-login-xcy') {
  const lock = net.createServer((socket) => socket.destroy());
  lock.unref();
  return new Promise((resolve, reject) => {
    lock.once('error', (error) => error.code === 'EADDRINUSE' ? resolve(null) : reject(error));
    lock.listen(pipe, () => resolve(lock));
  });
}

async function main() {
  const lock = await acquireStartupLock();
  if (!lock) { log('自动重连已在运行'); return; }
  log('开始登录后自动启动，每5秒检查机器人连接');
  launch('费用监控', process.execPath, [path.join(root, 'scripts/spend-watch.mjs')]);
  launch('板卡报警监测', process.execPath, [path.join(root, 'scripts/board-watch.mjs')]);
  if (!await available(cfg.dsh.baseUrl)) {
    launch('DSH', path.join(process.env.LOCALAPPDATA, 'Programs/DSH Desktop/DSH Desktop.exe'));
  }
  if (!await available(snowUrl)) {
    launch('SnowLuma', path.join(snowHome, 'node.exe'), [path.join(snowHome, 'index.mjs')], snowHome);
  }
  if (!await available(bridgeUrl)) launch('QQ Bridge', process.execPath, [path.join(root, 'src/bridge.js')]);
  let headers, checkedInitialAccounts = false, lastStatus;
  const attempt = {};
  while (true) {
    let status;
    try {
      headers ??= await snowHeaders();
      const { list } = await request(`${snowUrl}/api/processes`, headers);
      if (!checkedInitialAccounts) {
        checkedInitialAccounts = true;
        const { missing } = planQQAccounts(list);
        if (missing.length) {
          log(`等待 QQ 自带自动登录：${missing.join('、')}`);
          launch('QQ 双账号自动登录', path.join(process.env.ProgramFiles, 'Tencent/QQNT/QQ.exe'));
        }
      }
      status = await reconnectBot(list, attempt, Date.now(),
        (pid) => request(`${snowUrl}/api/processes/${pid}/load`, headers, {}));
      if (status === 'online') {
        const account = await request(`${cfg.snowluma.httpUrl.replace(/\/+$/, '')}/get_login_info`, {
          authorization: `Bearer ${cfg.snowluma.accessToken}`,
        }, {});
        assert.equal(String(account.data?.user_id), botUin, 'OneBot 连接的不是指定机器人账号');
        const bridge = await request(`${bridgeUrl}/api/status`, { 'x-console-token': cfg.consoleToken });
        status = bridge.dshReady && bridge.snowluma?.accountOnline && bridge.snowluma?.connected && bridge.snowluma?.good
          ? '启动完成：DSH、QQ Bridge、机器人 QQ 均已连接；持续自动重连中'
          : '机器人已登录，等待 QQ Bridge 就绪';
      }
    } catch (error) {
      headers = undefined;
      status = `自动连接等待重试：${error.message}`;
    }
    if (status !== lastStatus) { log(status); lastStatus = status; }
    await sleep(5000);
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main().catch((error) => {
  log(`自动启动未完成：${error.message}`);
  process.exitCode = 1;
});
