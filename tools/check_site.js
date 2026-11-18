#!/usr/bin/env node
/**
 * check_site.js —— 站点数据自检（不需要编译器，Node 直接跑）
 *
 * 检查 web/ 下这套静态站点能否正常打开：
 *   1) traces/catalog.js 里每个场景的数据文件都存在、能解析
 *   2) 每份 trace 的 targets / plans / events 字段齐全
 *   3) 每段 legs 的距离与“目标漂移 + 飞船匀速”反推出来的距离一致
 *      （不一致就说明 C 端导出的不是 t=0 初始坐标，网页里会出现航线错位）
 *   4) index.html 引用的 styles.css / boot.js / traces/catalog.js 都存在
 *
 * 用法：node tools/check_site.js [web目录，默认 web]
 * 退出码：0 全部通过；1 有问题
 */
'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const webDir = path.resolve(process.argv[2] || 'web');
const problems = [];
const notes = [];

function fail(msg) { problems.push(msg); }
function ok(msg) { notes.push(msg); }

function loadCatalog() {
  const file = path.join(webDir, 'traces', 'catalog.js');
  if (!fs.existsSync(file)) { fail('缺少 traces/catalog.js'); return []; }
  const sandbox = { window: {} };
  vm.createContext(sandbox);
  try {
    vm.runInContext(fs.readFileSync(file, 'utf8'), sandbox, { filename: file });
  } catch (e) {
    fail('catalog.js 语法错误：' + e.message);
    return [];
  }
  const cat = sandbox.window.RESCUE_TRACE_CATALOG;
  if (!Array.isArray(cat) || !cat.length) { fail('catalog.js 里没有场景清单'); return []; }
  ok('catalog.js 共 ' + cat.length + ' 个场景');
  return cat;
}

function loadTrace(relFile) {
  const file = path.join(webDir, relFile);
  if (!fs.existsSync(file)) { fail('场景数据缺失：' + relFile); return null; }
  const sandbox = { window: {} };
  vm.createContext(sandbox);
  try {
    vm.runInContext(fs.readFileSync(file, 'utf8'), sandbox, { filename: file });
  } catch (e) {
    fail(relFile + ' 语法错误：' + e.message);
    return null;
  }
  const t = sandbox.window.RESCUE_TRACE;
  if (!t) { fail(relFile + ' 没有设置 window.RESCUE_TRACE'); return null; }
  return t;
}

function dist(a, b) {
  const dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return Math.sqrt(dx * dx + dy * dy + dz * dz);
}

function checkTrace(entry, tr) {
  const tag = entry.key;

  ['meta', 'targets', 'plans', 'events'].forEach((k) => {
    if (!(k in tr)) fail(tag + '：缺少字段 ' + k);
  });
  if (!tr.meta) return;
  if (!tr.meta.dataSource) fail(tag + '：meta 缺少 dataSource（前端徽标要用）');

  if (tr.targets.length !== tr.meta.count) {
    fail(tag + '：targets 数量 ' + tr.targets.length + ' 与 meta.count ' + tr.meta.count + ' 不一致');
  }
  if (!tr.plans.length) fail(tag + '：没有方案');

  const origin = tr.meta.origin || [0, 0, 0];
  const posAt = (id, t) => {
    const g = tr.targets[id];
    return [g.p0[0] + g.v[0] * t, g.p0[1] + g.v[1] * t, g.p0[2] + g.v[2] * t];
  };

  let legs = 0, maxErr = 0;
  tr.plans.forEach((pl) => {
    ['route', 'legs', 'outcome', 'saved', 'length', 'finish'].forEach((k) => {
      if (!(k in pl)) fail(tag + '/' + pl.name + '：缺少字段 ' + k);
    });
    (pl.legs || []).forEach((lg) => {
      const start = (lg.from < 0) ? origin : posAt(lg.from, lg.t0);
      const end = posAt(lg.to, lg.t1);
      const err = Math.abs(dist(start, end) - lg.dist);
      legs++;
      if (err > maxErr) maxErr = err;
      if (err > Math.max(0.05, lg.dist * 0.002)) {
        fail(tag + '/' + pl.name + '：T' + (lg.to + 1) + ' 段距离反推不一致（差 ' + err.toFixed(3) + '）');
      }
    });
  });

  const best = tr.plans[tr.meta.selectedPlan] || tr.plans[0];
  ok(tag + '：' + tr.targets.length + ' 目标 / ' + tr.plans.length + ' 方案 / ' +
     legs + ' 段航程，最长距离误差 ' + maxErr.toFixed(4) + '，推荐方案获救 ' +
     best.saved + '/' + tr.meta.count + '（' + (tr.meta.dataSource === 'mock' ? '测试桩' : '正式模块') + '）');
}

function checkHtmlRefs() {
  const idx = path.join(webDir, 'index.html');
  if (!fs.existsSync(idx)) { fail('缺少 index.html'); return; }
  const html = fs.readFileSync(idx, 'utf8');
  const refs = [];
  const re = /(?:href|src)="([^"]+)"/g;
  let m;
  while ((m = re.exec(html))) {
    const p = m[1];
    if (/^(https?:|\/\/|#|data:)/.test(p)) continue;
    refs.push(p);
  }
  refs.forEach((p) => {
    const f = path.join(webDir, p.split('?')[0]);
    if (!fs.existsSync(f)) fail('index.html 引用了不存在的文件：' + p);
  });
  ok('index.html 引用的本地资源 ' + refs.length + ' 个都在');
}

const catalog = loadCatalog();
catalog.forEach((entry) => {
  if (!entry.file) { fail('场景 ' + entry.key + ' 没有 file 字段'); return; }
  const tr = loadTrace(entry.file);
  if (tr) checkTrace(entry, tr);
});
checkHtmlRefs();

notes.forEach((n) => console.log('  √ ' + n));
if (problems.length) {
  console.log('\n  共 ' + problems.length + ' 个问题：');
  problems.slice(0, 30).forEach((p) => console.log('  × ' + p));
  process.exit(1);
}
console.log('\n  站点数据自检通过（' + notes.length + ' 项）');
