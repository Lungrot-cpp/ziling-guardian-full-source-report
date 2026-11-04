import assert from 'node:assert/strict';
import { audit } from './spend-watch.mjs';
import { DEFAULT_PRICE_TABLE } from '../src/model-prices.js';

const now = Date.parse('2026-10-29T15:00:00Z');
const request = { sessionId: 's', turn: 1, step: 1, retry: 0, time: now,
  model: 'deepseek-flash', tools: ['compress'], usage: { inputTokens: 1000000, outputTokens: 250000 } };
const failure = { sessionId: 's', turn: 1, tool: 'compress', time: now - 1000 };
const session = { requests: [request], failures: [failure, failure, failure], activeTurn: 1, lastTime: now };
let result = audit({ sessions: [session] }, now, DEFAULT_PRICE_TABLE);
assert.equal(result.total, 2);
assert.equal(result.byPurpose['压缩上下文'], 2);
assert.equal(result.loops.length, 1);
assert.equal(audit({ sessions: [{ ...session, activeTurn: null }] }, now, DEFAULT_PRICE_TABLE).loops.length, 0);
assert.equal(audit({ sessions: [{ ...session, failures: [failure, failure] }] }, now, DEFAULT_PRICE_TABLE).loops.length, 0);
assert.equal(audit({ sessions: [session] }, now + 180000, DEFAULT_PRICE_TABLE).loops.length, 0);
result = audit({ sessions: [{ ...session, requests: [request, { ...request, time: Date.parse('2026-10-28T15:59:59Z') }] }] }, now, DEFAULT_PRICE_TABLE);
assert.equal(result.rows.length, 1, 'Beijing midnight excludes yesterday');
assert.equal(audit({ sessions: [session] }, Date.parse('2026-10-29T16:00:00Z'), DEFAULT_PRICE_TABLE).total, 0);
console.log('费用计算、北京时间跨日、重复失败与正常回合隔离：通过');
