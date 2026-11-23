/* Node 侧自检：验证 web/random.js 生成的 trace 满足数据契约
 *   1) targets/plans/events 结构齐全
 *   2) plans[].legs 的 dist 与 “p0+v·t 反推的直线距离”一致（网页回放依赖这个）
 *   3) 多个 seed 都能出结果、时间有限
 * 用法：node tools/check_random.js
 */
'use strict';

const path = require('path');
const { buildTrace } = require(path.join(__dirname, '..', 'web', 'random.js'));

function dist(a, b) {
  const dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return Math.sqrt(dx * dx + dy * dy + dz * dz);
}

let problems = 0;
function fail(msg) { problems++; console.log('  x ' + msg); }

for (const seed of [1, 42, 2026, 987654321, 7]) {
  const t0 = Date.now();
  const tr = buildTrace({ seed });
  const ms = Date.now() - t0;

  const n = tr.targets.length;
  if (!tr.plans.length) fail(`seed=${seed} 没有方案`);
  if (!tr.events.length) fail(`seed=${seed} 没有事件`);
  if (tr.meta.count !== n) fail(`seed=${seed} meta.count 不一致`);
  if (tr.meta.dataSource !== 'client') fail(`seed=${seed} dataSource 不是 client`);

  const posAt = (id, t) => {
    const g = tr.targets[id];
    return [g.p0[0] + g.v[0] * t, g.p0[1] + g.v[1] * t, g.p0[2] + g.v[2] * t];
  };

  let maxErr = 0, legs = 0;
  tr.plans.forEach((p) => {
    if (p.route.length !== p.legs.length) fail(`seed=${seed}/${p.name} route 与 legs 数量不一致`);
    p.legs.forEach((lg) => {
      const start = lg.from < 0 ? [0, 0, 0] : posAt(lg.from, lg.t0);
      const end = posAt(lg.to, lg.t1);
      const err = Math.abs(dist(start, end) - lg.dist);
      legs++;
      if (err > maxErr) maxErr = err;
      if (err > 0.05) fail(`seed=${seed}/${p.name} 第 ${lg.to + 1} 段距离反推不一致（差 ${err.toFixed(3)}）`);
    });
    if (!isFinite(p.length) || p.length <= 0) fail(`seed=${seed}/${p.name} 航程异常`);
    const savedFromOutcome = p.outcome.filter((o) => o.state === 'saved').length;
    if (savedFromOutcome !== p.saved) fail(`seed=${seed}/${p.name} outcome 与 saved 不一致`);
  });

  const sel = tr.plans[tr.meta.selectedPlan];
  console.log(`  √ seed=${seed.toString().padStart(9)}  n=${String(n).padStart(2)}  方案=${tr.plans.length}` +
              `  推荐=${sel.name}(${sel.saved}/${n}, ${sel.length.toFixed(1)})` +
              `  航段=${legs}  最大误差=${maxErr.toFixed(4)}  耗时=${ms}ms`);
}

if (problems) {
  console.log(`\n  共 ${problems} 个问题`);
  process.exit(1);
}
console.log('\n  random.js 生成的数据符合契约');
