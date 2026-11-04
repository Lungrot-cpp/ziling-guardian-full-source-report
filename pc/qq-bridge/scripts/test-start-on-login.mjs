import assert from 'node:assert/strict';
import { planQQAccounts, reconnectBot, acquireStartupLock } from './start-on-login.mjs';

const personal = { pid: 1, account: { uin: '123456789' } };
const bot = { pid: 2, account: { uin: 3771755152 } };
assert.deepEqual(planQQAccounts([]).missing, ['3771755152', '123456789']);
assert.deepEqual(planQQAccounts([personal]).missing, ['3771755152']);
assert.equal(planQQAccounts([personal]).bot, undefined, 'personal account must never be hooked as the bot');
assert.deepEqual(planQQAccounts([bot]).missing, ['123456789']);
assert.deepEqual(planQQAccounts([personal, bot]).missing, []);
assert.equal(planQQAccounts([personal, bot]).bot, bot);
assert.deepEqual(planQQAccounts([{ uin: '3771755152' }, { uin: '123456789' }]).missing, []);
console.log('双账号启动检查、账号类型兼容与机器人账号隔离：通过');

const loads = [], attempt = {};
const load = async (pid) => { loads.push(pid); return { success: true }; };
const oldBot = { ...bot, connected: false, injected: false };
await reconnectBot([personal, { pid: 3, uin: '0' }], attempt, 0, load);
assert.deepEqual(loads, [], 'personal and unidentified QQ processes must never be injected');
await reconnectBot([personal, oldBot], attempt, 0, load);
await reconnectBot([oldBot], attempt, 29999, load);
assert.deepEqual(loads, [2], 'do not repeatedly inject a process still connecting');
await reconnectBot([oldBot], attempt, 30000, load);
assert.deepEqual(loads, [2, 2], 'failed connection can retry after 30 seconds');
await reconnectBot([], attempt, 30001, load);
const newBot = { ...oldBot, pid: 4 };
await reconnectBot([personal, newBot], attempt, 30002, load);
assert.deepEqual(loads, [2, 2, 4], 'a new QQ process reconnects without the old process cooldown');
assert.match(await reconnectBot([{ ...newBot, connected: true, loggedIn: false }], attempt, 70000, load), /安全验证/);
assert.equal(await reconnectBot([{ ...newBot, connected: true, loggedIn: true }], attempt, 70001, load), 'online');
await reconnectBot([{ ...newBot, status: 'loading' }], attempt, 70002, load);
assert.deepEqual(loads, [2, 2, 4], 'online, awaiting login and loading processes must be left alone');
await reconnectBot([{ ...newBot, injected: true }], attempt, 70003, load);
assert.deepEqual(loads, [2, 2, 4, 4], 'an injected process with a dropped pipe can reconnect');
await assert.rejects(reconnectBot([{ ...newBot, pid: -1 }], {}, 0, load), /进程号/);
const failed = {};
await assert.rejects(reconnectBot([oldBot], failed, 0, async () => ({ success: false })), /未成功/);
await reconnectBot([oldBot], failed, 1, load);
assert.equal(loads.length, 4, 'an injection failure must also respect retry backoff');

const acquire = () => acquireStartupLock(`\\\\.\\pipe\\qq-bridge-start-on-login-test-${process.pid}`);
const first = await acquire();
assert(first);
try { assert.equal(await acquire(), null, 'a second startup must not create another reconnect loop'); }
finally { await new Promise((resolve) => first.close(resolve)); }
const restarted = await acquire();
assert(restarted, 'the lock must be released when the first guard exits');
await new Promise((resolve) => restarted.close(resolve));
console.log('退出再登录、新进程立即注入、30秒重试、扫码等待、在线不重复注入与单实例：通过');
