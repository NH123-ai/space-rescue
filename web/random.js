/*============================================================================
 *  random.js  —  “随机”按钮：在本机浏览器里现生成一局新场景
 *----------------------------------------------------------------------------
 *  页面上的场景数据平时是 C 程序预算好、提交进仓库的（web/traces/*）。
 *  这个脚本提供一个 `RESCUE_RANDOM.buildTrace()`，用同一套模型（目标匀速漂移、
 *  飞船恒速、生存时间约束）在浏览器里现场随机生成目标位置/速度/生存时间，
 *  并用四种策略求解，产出与 traces/*.js 完全相同结构的一份 trace。
 *
 *  按下按钮的流程：buildTrace() -> 存进 sessionStorage -> 跳到 ?rand=1 重新加载，
 *  这样时间轴自然从 0 开始，页面其它逻辑（时间轴、面板、对比）都不用改。
 *
 *  算法与 trace 结构都对齐 C 端（viz_module.c / plan_adapter.c）：
 *    · 拦截时间用二次方程解析解，而不是 距离/速度；
 *    · plans[].legs 每段 {from,to,t0,t1,dist}，dist = SPEED × 段时长；
 *    · events 里给出 launch / refresh / saved / dead / finish 五类。
 *===========================================================================*/
(function (global) {
  'use strict';

  var SPEED = 10.0;
  var INTERVAL = 5.0;
  var MAXN = 12;
  var NODE_CAP = 200000;      /* 修剪回溯的节点上限：保证点一下几十毫秒内出结果 */

  var NAMES = {
    0: '最近邻贪心 NN',
    1: '生存时间优先 EDF',
    2: '回溯搜索 (剪枝)',
    3: '2-opt 局部搜索'
  };

  /* ---------------- 随机数：与 C 端同款 32 位 LCG（同一 seed 结果一致） --------------- */
  function makeRng(seed) {
    var s = ((seed >>> 0) * 2654435761 + 12345) >>> 0;
    return {
      next: function () {
        s = (Math.imul(s, 1103515245) + 12345) >>> 0;
        return ((s >>> 16) & 0x7FFF) / 32767;
      },
      range: function (lo, hi) {
        return lo + (hi - lo) * this.next();
      }
    };
  }

  /* ---------------- 几何 ---------------- */
  function posAt(tg, t) {
    return [tg.p0[0] + tg.v[0] * t, tg.p0[1] + tg.v[1] * t, tg.p0[2] + tg.v[2] * t];
  }

  /* 从 (sx,sy,sz)、时刻 tBegin 出发追上目标所需时间（一元二次方程取正根） */
  function interceptTime(tg, tBegin, sx, sy, sz) {
    var p = posAt(tg, tBegin);
    var dx = p[0] - sx, dy = p[1] - sy, dz = p[2] - sz;
    var a = tg.v[0] * tg.v[0] + tg.v[1] * tg.v[1] + tg.v[2] * tg.v[2] - SPEED * SPEED;
    var b = 2 * (dx * tg.v[0] + dy * tg.v[1] + dz * tg.v[2]);
    var c = dx * dx + dy * dy + dz * dz;
    if (Math.abs(a) < 1e-12) return Math.abs(b) < 1e-12 ? 0 : -c / b;
    var disc = b * b - 4 * a * c;
    if (disc < 0) return Math.sqrt(c) / SPEED;
    var r1 = (-b + Math.sqrt(disc)) / (2 * a);
    var r2 = (-b - Math.sqrt(disc)) / (2 * a);
    var lo = Math.min(r1, r2), hi = Math.max(r1, r2);
    if (lo >= 0) return lo;
    return hi > 0 ? hi : 0;
  }

  /* ---------------- 场景生成 ---------------- */
  function genTargets(count, rng) {
    var list = [];
    var i;
    for (i = 0; i < count; i++) {
      var u1 = rng.next(), u2 = rng.next(), u3 = rng.next();
      var radius = 50 + 110 * Math.cbrt(u1);
      var theta = 6.283185307 * u2;
      var phi = Math.acos(2 * u3 - 1);
      var x = radius * Math.sin(phi) * Math.cos(theta);
      var y = radius * Math.cos(phi) * 0.75;
      var z = radius * Math.sin(phi) * Math.sin(theta);

      var vt = rng.range(0, 6.283185307);
      var vp = rng.range(-1, 1);
      var sp = rng.range(-0.6, 0.6);
      var cs = Math.sqrt(1 - vp * vp);
      var vx = sp * cs * Math.cos(vt), vy = sp * vp, vz = sp * cs * Math.sin(vt);

      /* 生存时间先给一个大值，等算完“最近邻”的到达时刻再按真实节奏收紧，
       * 见 buildTrace() 里的二次定标 —— 这样绝大多数目标能救到，偶尔留一两个
       * 紧张的，不会出现“点一下全死光”的无聊局面。 */
      var dist = Math.sqrt(x * x + y * y + z * z);
      var eta = dist / SPEED;
      var life = eta * 6 + 60;

      list.push({
        id: i, name: 'T' + (i + 1),
        p0: [x, y, z], v: [vx, vy, vz],
        life: life, dist0: dist, eta0: eta
      });
    }
    return list;
  }

  /* ---------------- 按给定顺序推演一遍 ---------------- */
  function simulate(targets, order) {
    var done = {}, t = 0, sx = 0, sy = 0, sz = 0;
    var seq = [], legT = [], legD = [], saved = 0, dead = 0;

    order.forEach(function (idx) {
      if (done[idx]) return;
      done[idx] = true;
      var tg = targets[idx];
      var tau = interceptTime(tg, t, sx, sy, sz);
      var arrive = t + tau;
      if (arrive > tg.life) { dead++; return; }
      var p = posAt(tg, arrive);
      seq.push(idx);
      legT.push(arrive);
      legD.push(tau * SPEED);
      sx = p[0]; sy = p[1]; sz = p[2];
      t = arrive;
      saved++;
    });

    targets.forEach(function (tg, i) { if (!done[i]) dead++; });

    var length = legD.reduce(function (a, b) { return a + b; }, 0);
    return {
      seq: seq, legT: legT, legD: legD, saved: saved, dead: dead,
      length: length, finish: t, feasible: dead === 0
    };
  }

  /* ---------------- 四种策略 ---------------- */
  function planNN(targets) {
    var used = {}, order = [], t = 0, sx = 0, sy = 0, sz = 0, nodes = 0;
    for (var step = 0; step < targets.length; step++) {
      var best = -1, bestTau = Infinity;
      for (var i = 0; i < targets.length; i++) {
        if (used[i]) continue;
        var tau = interceptTime(targets[i], t, sx, sy, sz);
        nodes++;
        if (tau < bestTau) { bestTau = tau; best = i; }
      }
      if (best < 0) break;
      used[best] = true;
      order.push(best);
      if (t + bestTau <= targets[best].life) {
        var p = posAt(targets[best], t + bestTau);
        sx = p[0]; sy = p[1]; sz = p[2];
        t += bestTau;
      }
    }
    return { plan: simulate(targets, order), nodes: nodes };
  }

  function planEDF(targets) {
    var used = {}, order = [], t = 0, sx = 0, sy = 0, sz = 0, nodes = 0;
    for (var step = 0; step < targets.length; step++) {
      var best = -1, bestSlack = Infinity, bestTau = 0;
      for (var i = 0; i < targets.length; i++) {
        if (used[i]) continue;
        var tau = interceptTime(targets[i], t, sx, sy, sz);
        var slack = targets[i].life - (t + tau);
        nodes++;
        if (slack < bestSlack) { bestSlack = slack; best = i; bestTau = tau; }
      }
      if (best < 0) break;
      used[best] = true;
      order.push(best);
      if (t + bestTau <= targets[best].life) {
        var p = posAt(targets[best], t + bestTau);
        sx = p[0]; sy = p[1]; sz = p[2];
        t += bestTau;
      }
    }
    return { plan: simulate(targets, order), nodes: nodes };
  }

  function plan2opt(targets, base) {
    var cur = base.plan, nodes = base.nodes, improved = true, rounds = 0;
    while (improved && rounds < 6) {
      improved = false;
      rounds++;
      for (var i = 0; i < cur.seq.length - 1; i++) {
        for (var j = i + 1; j < cur.seq.length; j++) {
          var cand = cur.seq.slice(0, i).concat(cur.seq.slice(i, j + 1).reverse(), cur.seq.slice(j + 1));
          var trial = simulate(targets, cand);
          nodes++;
          if (trial.saved > cur.saved || (trial.saved === cur.saved && trial.length < cur.length - 1e-9)) {
            cur = trial;
            improved = true;
          }
        }
      }
    }
    return { plan: cur, nodes: nodes };
  }

  /* 剪枝回溯：获救数优先，其次航程；节点有上限 */
  function planPrune(targets) {
    var n = targets.length, best = { seq: [], saved: -1, length: Infinity, dead: n, finish: 0, feasible: false, legT: [], legD: [] };
    var nodes = 0, path = [], visited = {};

    function dfs(t, sx, sy, sz, length) {
      if (++nodes > NODE_CAP) return true;
      for (var i = 0; i < n; i++) {
        if (visited[i]) continue;
        var tau = interceptTime(targets[i], t, sx, sy, sz);
        var arrive = t + tau;
        if (arrive > targets[i].life) continue;                       /* 永远救不到 */
        if (best.saved === n && length + tau * SPEED >= best.length - 1e-9) continue;
        var p = posAt(targets[i], arrive);
        visited[i] = true;
        path.push(i);
        if (path.length > best.saved ||
            (path.length === best.saved && length + tau * SPEED < best.length)) {
          var sim = simulate(targets, path);
          if (sim.saved > best.saved ||
              (sim.saved === best.saved && sim.length < best.length)) best = sim;
        }
        if (dfs(arrive, p[0], p[1], p[2], length + tau * SPEED)) { path.pop(); visited[i] = false; return true; }
        path.pop();
        visited[i] = false;
      }
      return false;
    }

    dfs(0, 0, 0, 0, 0);
    if (!best.seq.length) {
      var fallback = planNN(targets);
      return { plan: fallback.plan, nodes: nodes + fallback.nodes };
    }
    return { plan: best, nodes: nodes };
  }

  /* ---------------- 组装 trace（结构与 traces/*.js 一致） ---------------- */
  function buildTrace(opt) {
    var o = opt || {};
    var seed = o.seed != null ? (o.seed >>> 0) : ((Math.random() * 0xFFFFFFFF) >>> 0);
    var rng = makeRng(seed);
    var count = o.count || (6 + Math.floor(rng.next() * 5));         /* 6~10 个目标 */
    if (count > MAXN) count = MAXN;

    var targets = genTargets(count, rng);
    var t0 = (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now();

    /* 二次定标生存时间：先用“最近邻”跑一遍拿到真实到达时刻，再按这个节奏给每个
     * 目标一个略宽/略紧的生存上限。最后到达的那个故意卡紧一点，制造“差一点救不到”
     * 的紧张感；其余留出余量，所以推荐方案通常能救回绝大多数目标。 */
    (function calibrate() {
      var probe = planNN(targets);
      var last = probe.plan.seq.length - 1;
      /* 这一局的“松紧程度”先摇一下：多数局偏宽松（基本都能救到），
       * 少数局紧一点，制造需要换策略才能多救一个的看点。 */
      var mood = rng.next();
      var tightProb = mood < 0.40 ? 0.08 : (mood < 0.78 ? 0.18 : 0.30);

      probe.plan.seq.forEach(function (idx, k) {
        var arrive = probe.plan.legT[k];
        var tight = (k === last && rng.next() < 0.5) || (rng.next() < tightProb);
        var factor = tight ? rng.range(0.95, 1.08) : rng.range(1.12, 1.55);
        var slack = tight ? 0 : rng.range(0, 8);
        targets[idx].life = Math.max(arrive * factor + slack, 3);
      });
    })();

    var nn = planNN(targets);
    var edf = planEDF(targets);
    var better = (edf.plan.saved > nn.plan.saved ||
                 (edf.plan.saved === nn.plan.saved && edf.plan.length < nn.plan.length)) ? edf : nn;
    var opt2 = plan2opt(targets, better);
    var bt = planPrune(targets);
    var t1 = (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now();

    var raw = [
      { algo: 0, name: NAMES[0], plan: nn.plan, nodes: nn.nodes },
      { algo: 1, name: NAMES[1], plan: edf.plan, nodes: edf.nodes },
      { algo: 3, name: NAMES[3], plan: opt2.plan, nodes: opt2.nodes },
      { algo: 2, name: NAMES[2], plan: bt.plan, nodes: bt.nodes }
    ];

    var totalMs = Math.max(t1 - t0, 0.01);

    var plans = raw.map(function (r, idx) {
      var p = r.plan;
      var legs = [];
      for (var k = 0; k < p.seq.length; k++) {
        legs.push({
          from: k === 0 ? -1 : p.seq[k - 1],
          to: p.seq[k],
          t0: +(k === 0 ? 0 : p.legT[k - 1]).toFixed(3),
          t1: +p.legT[k].toFixed(3),
          dist: +p.legD[k].toFixed(3)
        });
      }
      var outcome = [];
      for (var i = 0; i < targets.length; i++) {
        var at = p.seq.indexOf(i);
        if (at >= 0) outcome.push({ id: i, state: 'saved', t: +p.legT[at].toFixed(3) });
        else outcome.push({ id: i, state: 'dead', t: +targets[i].life.toFixed(3) });
      }
      return {
        id: idx, algo: r.algo, name: r.name,
        saved: p.saved, dead: p.dead, feasible: p.feasible ? 1 : 0,
        length: +p.length.toFixed(3), finish: +p.finish.toFixed(3),
        nodes: r.nodes, elapsedMs: +(totalMs / raw.length).toFixed(3),
        route: p.seq, legs: legs, outcome: outcome
      };
    });

    /* 推荐方案：获救数最多、其次航程最短 */
    var selected = 0;
    for (var i = 1; i < plans.length; i++) {
      if (plans[i].saved > plans[selected].saved) selected = i;
      else if (plans[i].saved === plans[selected].saved && plans[i].length < plans[selected].length) selected = i;
    }
    var sel = plans[selected];

    /* 事件流 */
    var events = [{ t: 0, type: 'launch', target: -1, text: '救援飞船离开发射基点' }];
    var rt = INTERVAL, round = 1;
    while (rt < sel.finish + 1e-9) {
      events.push({ t: +rt.toFixed(3), type: 'refresh', target: -1, text: '收到目标最新坐标（第 ' + round + ' 次刷新）' });
      rt += INTERVAL;
      round++;
    }
    sel.legs.forEach(function (lg) {
      events.push({ t: lg.t1, type: 'saved', target: lg.to, text: 'T' + (lg.to + 1) + ' 完成对接，生存时间锁定' });
    });
    sel.outcome.forEach(function (o2) {
      if (o2.state === 'dead') {
        events.push({ t: o2.t, type: 'dead', target: o2.id, text: 'T' + (o2.id + 1) + ' 生存时间耗尽，救援失败' });
      }
    });
    events.push({ t: sel.finish, type: 'finish', target: -1, text: '救援任务结束' });

    var now = new Date();
    function pad(v) { return (v < 10 ? '0' : '') + v; }
    var stamp = now.getFullYear() + '-' + pad(now.getMonth() + 1) + '-' + pad(now.getDate()) +
                ' ' + pad(now.getHours()) + ':' + pad(now.getMinutes()) + ':' + pad(now.getSeconds());

    return {
      meta: {
        title: '太空救援任务 · 最优救援序列可视化',
        generated: stamp,
        scenario: 4,
        scenarioName: '本机随机场景',
        seed: seed,
        count: targets.length,
        speed: SPEED,
        updateInterval: INTERVAL,
        maxTargets: MAXN,
        origin: [0, 0, 0],
        dataSource: 'client',
        selectedPlan: selected
      },
      targets: targets,
      plans: plans,
      events: events
    };
  }

  global.RESCUE_RANDOM = { buildTrace: buildTrace };
  if (typeof module !== 'undefined' && module.exports) module.exports = { buildTrace: buildTrace };
})(typeof window !== 'undefined' ? window : globalThis);
