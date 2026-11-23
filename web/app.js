/*============================================================================
 *  app.js  —  太空救援任务 3D 可视化（Canvas 2D + 手写三维投影，零依赖）
 *----------------------------------------------------------------------------
 *  数据契约：window.RESCUE_TRACE（由 C 程序 viz_write_trace 生成）
 *    meta    : 场景参数（速度 / 刷新间隔 / 推荐方案下标 …）
 *    targets : [{id,name,p0:[x,y,z], v:[vx,vy,vz], life, dist0, eta0}]
 *    plans   : [{id,name,saved,dead,feasible,length,finish,nodes,elapsedMs,
 *                route:[...], legs:[{from,to,t0,t1,dist}], outcome:[...]}]
 *    events  : [{t,type,target,text}]
 *
 *  说明：轨迹不再由 C 逐帧导出，而是“目标等速漂移 + 飞船分段匀速”这两条
 *        解析规律在浏览器端还原，因此时间轴可以任意拖拽、倍速、切换方案。
 *=========================================================================*/
(function () {
  'use strict';

  var D = window.RESCUE_TRACE;

  /* =======================================================================
   *  0. 无数据兜底
   * ==================================================================== */
  if (!D || !D.targets || !D.plans || !D.plans.length) {
    document.body.innerHTML =
      '<div style="height:100vh;display:grid;place-items:center;color:#a9b8cf;' +
      'font-family:system-ui,sans-serif;text-align:center;line-height:2">' +
      '<div><h2 style="color:#35d6f5;letter-spacing:.1em">未找到救援轨迹数据</h2>' +
      '<p>请先在 C 工程目录运行（任意其一）：</p>' +
      '<pre style="color:#ffb245;font-size:13px">space_rescue --trace web</pre>' +
      '<pre style="color:#ffb245;font-size:13px">space_rescue --n 9 --scenario tight --seed 2026</pre>' +
      '<p>生成 <code>web/trace.js</code> 后再刷新本页。</p></div></div>';
    return;
  }

  /* =======================================================================
   *  1. 常量与工具
   * ==================================================================== */
  var C = {
    cyan:  '#35d6f5',
    amber: '#ffb245',
    green: '#37e0a0',
    red:   '#ff5c72',
    violet:'#9b8cff',
    grey:  '#6c7c95',
    ink:   '#e8f1ff'
  };

  var TAU = Math.PI * 2;

  function clamp(v, a, b) { return v < a ? a : (v > b ? b : v); }
  function lerp(a, b, k) { return a + (b - a) * k; }
  function fmt(v, d) { return Number(v).toFixed(d === undefined ? 2 : d); }
  function sub(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
  function add(a, b) { return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]; }
  function scale(a, k) { return [a[0] * k, a[1] * k, a[2] * k]; }
  function len3(a) { return Math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }

  function rgba(hex, a) {
    var n = parseInt(hex.slice(1), 16);
    return 'rgba(' + ((n >> 16) & 255) + ',' + ((n >> 8) & 255) + ',' + (n & 255) + ',' + a + ')';
  }

  /* =======================================================================
   *  2. 解析轨迹：waypoint 序列 + 各时刻状态
   * ==================================================================== */
  var T = D.targets;                       /* 目标数组（下标即 id） */
  var PLANS = D.plans;
  var META = D.meta || {};
  var ORIGIN = META.origin || [0, 0, 0];
  var SPEED = META.speed || 10;
  var REFRESH = META.updateInterval || 5;

  function targetPos(tg, t) {
    return [tg.p0[0] + tg.v[0] * t, tg.p0[1] + tg.v[1] * t, tg.p0[2] + tg.v[2] * t];
  }

  /* 一个方案展开成航路点：[{p, t0, t1, target}]，首点 = 原点 */
  function buildRoute(plan) {
    var wps = [];
    var legs = plan.legs || [];
    var prevP = ORIGIN.slice();
    var prevT = 0;
    for (var i = 0; i < legs.length; i++) {
      var lg = legs[i];
      var endP = targetPos(T[lg.to], lg.t1);
      wps.push({
        a: prevP, b: endP,
        t0: lg.t0, t1: lg.t1, target: lg.to, dist: lg.dist,
        from: lg.from
      });
      prevP = endP;
      prevT = lg.t1;
    }
    return { legs: wps, end: prevT };
  }

  var ROUTES = PLANS.map(buildRoute);

  /* 事件时间上界：保证牺牲目标也能播完 */
  function eventHorizon() {
    var m = 0;
    (D.events || []).forEach(function (e) { if (e.t > m) m = e.t; });
    PLANS.forEach(function (p) { if (p.finish > m) m = p.finish; });
    T.forEach(function (tg) { if (tg.life > m) m = tg.life; });
    return m;
  }

  /* =======================================================================
   *  3. 状态
   * ==================================================================== */
  var state = {
    planIdx: clamp(META.selectedPlan || 0, 0, PLANS.length - 1),
    t: 0,
    duration: Math.max(eventHorizon(), 1),
    playing: false,
    speed: 1,
    autoOrbit: true,
    showAllRoutes: false,
    hoverT: null,
    cam: { yaw: 0.72, pitch: 0.40, dist: 0 },
    bounds: null
  };

  var BASE_RATE = state.duration / 48;      /* 1× 时约 48 秒播完全程 */

  /* 场景包围盒：同时考虑漂移后的位置 */
  (function computeBounds() {
    var mn = [1e9, 1e9, 1e9], mx = [-1e9, -1e9, -1e9];
    var horizon = state.duration;
    T.forEach(function (tg) {
      var pts = [targetPos(tg, 0), targetPos(tg, horizon)];
      pts.forEach(function (p) {
        for (var i = 0; i < 3; i++) {
          if (p[i] < mn[i]) mn[i] = p[i];
          if (p[i] > mx[i]) mx[i] = p[i];
        }
      });
    });
    for (var i = 0; i < 3; i++) {
      mn[i] = Math.min(mn[i], ORIGIN[i]);
      mx[i] = Math.max(mx[i], ORIGIN[i]);
    }
    var center = [(mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, (mn[2] + mx[2]) / 2];
    var radius = Math.max(len3(sub(mx, mn)) * 0.5, 30);
    state.bounds = { min: mn, max: mx, center: center, radius: radius };
    state.cam.dist = radius * 3.1;
  })();

  /* =======================================================================
   *  4. 画布与相机
   * ==================================================================== */
  var cv = document.getElementById('stage');
  var ctx = cv.getContext('2d');
  var W = 0, H = 0, DPR = 1, VSCALE = 1, SCX = 0, SCY = 0;

  function resize() {
    DPR = Math.min(window.devicePixelRatio || 1, 2);
    W = cv.clientWidth;
    H = cv.clientHeight;
    cv.width = Math.round(W * DPR);
    cv.height = Math.round(H * DPR);
    ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
    SCX = W / 2;
    SCY = H / 2 + 8;
    VSCALE = 0.40 * Math.min(W, H) * state.cam.dist / state.bounds.radius;
  }
  window.addEventListener('resize', resize);

  function project(p) {
    var c = state.bounds.center;
    var x = p[0] - c[0], y = p[1] - c[1], z = p[2] - c[2];
    var cyw = Math.cos(state.cam.yaw), syw = Math.sin(state.cam.yaw);
    var x1 = x * cyw - z * syw;
    var z1 = x * syw + z * cyw;
    var cp = Math.cos(state.cam.pitch), sp = Math.sin(state.cam.pitch);
    var y2 = y * cp - z1 * sp;
    var z2 = y * sp + z1 * cp;
    var depth = z2 + state.cam.dist;
    if (depth < 10) return null;
    var s = VSCALE / depth;
    return { x: SCX + x1 * s, y: SCY - y2 * s, d: depth, s: s };
  }

  /* 光晕精灵（预渲染，避免每帧 createRadialGradient） */
  var glowCache = {};
  function glowSprite(hex) {
    if (glowCache[hex]) return glowCache[hex];
    var size = 96, c = document.createElement('canvas');
    c.width = c.height = size;
    var g = c.getContext('2d');
    var grd = g.createRadialGradient(size / 2, size / 2, 0, size / 2, size / 2, size / 2);
    grd.addColorStop(0, rgba(hex, 0.95));
    grd.addColorStop(0.28, rgba(hex, 0.42));
    grd.addColorStop(1, rgba(hex, 0));
    g.fillStyle = grd;
    g.fillRect(0, 0, size, size);
    glowCache[hex] = c;
    return c;
  }

  function drawGlow(p, hex, r, alpha) {
    var sp = glowSprite(hex);
    ctx.globalAlpha = alpha;
    ctx.drawImage(sp, p.x - r, p.y - r, r * 2, r * 2);
    ctx.globalAlpha = 1;
  }

  /* =======================================================================
   *  5. 星空与星云
   * ==================================================================== */
  var stars = [];
  var heroStars = [];
  (function makeStars() {
    var R = state.bounds.radius * 3.4;
    for (var i = 0; i < 420; i++) {
      var u = Math.random() * 2 - 1;
      var th = Math.random() * TAU;
      var r = R * (0.55 + Math.random() * 0.65);
      var s = Math.sqrt(1 - u * u);
      stars.push({
        p: [r * s * Math.cos(th), r * u, r * s * Math.sin(th)],
        b: 0.25 + Math.random() * 0.75,
        ph: Math.random() * TAU
      });
    }
    /* 少量“亮星”：带十字光芒，增强纵深感 */
    for (var k = 0; k < 5; k++) {
      var u2 = Math.random() * 2 - 1;
      var th2 = Math.random() * TAU;
      var r2 = R * (0.9 + Math.random() * 0.5);
      var s2 = Math.sqrt(1 - u2 * u2);
      heroStars.push({ p: [r2 * s2 * Math.cos(th2), r2 * u2, r2 * s2 * Math.sin(th2)],
                       huge: Math.random() > 0.5 });
    }
  })();

  /* 星云：几块低速漂移的彩色薄雾（静态配置，逐帧按正弦轻微起伏） */
  var nebulas = [
    { x: 0.30, y: 0.30, r: 0.52, hex: '#1b4d8f', a: 0.17, sp: 0.013 },
    { x: 0.72, y: 0.42, r: 0.46, hex: '#4a2a7a', a: 0.15, sp: 0.017 },
    { x: 0.52, y: 0.74, r: 0.44, hex: '#0d5f6b', a: 0.13, sp: 0.011 },
    { x: 0.86, y: 0.18, r: 0.30, hex: '#7a3d55', a: 0.10, sp: 0.021 }
  ];

  function drawNebula(time) {
    var R = Math.max(W, H);
    ctx.globalCompositeOperation = 'lighter';
    for (var i = 0; i < nebulas.length; i++) {
      var n = nebulas[i];
      var ox = Math.sin(time * n.sp + i) * W * 0.03;
      var oy = Math.cos(time * n.sp * 1.3 + i * 2) * H * 0.03;
      var cx = n.x * W + ox, cy = n.y * H + oy, rr = n.r * R;
      var g = ctx.createRadialGradient(cx, cy, 0, cx, cy, rr);
      g.addColorStop(0, rgba(n.hex, n.a));
      g.addColorStop(0.45, rgba(n.hex, n.a * 0.35));
      g.addColorStop(1, rgba(n.hex, 0));
      ctx.fillStyle = g;
      ctx.beginPath();
      ctx.arc(cx, cy, rr, 0, TAU);
      ctx.fill();
    }
    ctx.globalCompositeOperation = 'source-over';
  }

  function drawStars(time) {
    var i, s, pr, a, sz;

    for (i = 0; i < stars.length; i++) {
      s = stars[i];
      pr = project(s.p);
      if (!pr) continue;
      a = s.b * (0.6 + 0.4 * Math.sin(time * 1.6 + s.ph)) * clamp(320 / pr.d, 0.25, 1);
      if (a <= 0.02) continue;
      ctx.globalAlpha = a;
      ctx.fillStyle = i % 11 === 0 ? '#9be6ff' : '#dce9ff';
      sz = pr.d > 1 ? 1.6 : 1.1;
      ctx.fillRect(pr.x, pr.y, sz, sz);
    }
    ctx.globalAlpha = 1;

    /* 亮星 + 十字光芒 */
    for (i = 0; i < heroStars.length; i++) {
      pr = project(heroStars[i].p);
      if (!pr) continue;
      var tw = 0.5 + 0.5 * Math.sin(time * 1.1 + i);
      drawGlow(pr, '#cfe9ff', heroStars[i].huge ? 20 : 14, 0.30 + tw * 0.30);
      ctx.strokeStyle = 'rgba(220,240,255,' + (0.22 + tw * 0.25) + ')';
      ctx.lineWidth = 1;
      var L = heroStars[i].huge ? 13 : 8;
      ctx.beginPath();
      ctx.moveTo(pr.x - L, pr.y); ctx.lineTo(pr.x + L, pr.y);
      ctx.moveTo(pr.x, pr.y - L); ctx.lineTo(pr.x, pr.y + L);
      ctx.stroke();
    }
  }

  function drawFloorGrid() {
    var b = state.bounds;
    var y = b.min[1] - b.radius * 0.10;
    var ext = b.radius * 1.30;
    var step = b.radius / 5;
    var i, a, p1, p2;

    ctx.lineWidth = 1;
    for (i = -5; i <= 5; i++) {
      a = b.center[0] + i * step;
      p1 = project([a, y, b.center[2] - ext]);
      p2 = project([a, y, b.center[2] + ext]);
      if (p1 && p2) {
        var g1 = ctx.createLinearGradient(p1.x, p1.y, p2.x, p2.y);
        g1.addColorStop(0, 'rgba(53,214,245,0.02)');
        g1.addColorStop(0.5, 'rgba(53,214,245,0.13)');
        g1.addColorStop(1, 'rgba(53,214,245,0.02)');
        ctx.strokeStyle = g1;
        ctx.beginPath(); ctx.moveTo(p1.x, p1.y); ctx.lineTo(p2.x, p2.y); ctx.stroke();
      }
      a = b.center[2] + i * step;
      p1 = project([b.center[0] - ext, y, a]);
      p2 = project([b.center[0] + ext, y, a]);
      if (p1 && p2) {
        var g2 = ctx.createLinearGradient(p1.x, p1.y, p2.x, p2.y);
        g2.addColorStop(0, 'rgba(53,214,245,0.02)');
        g2.addColorStop(0.5, 'rgba(53,214,245,0.10)');
        g2.addColorStop(1, 'rgba(53,214,245,0.02)');
        ctx.strokeStyle = g2;
        ctx.beginPath(); ctx.moveTo(p1.x, p1.y); ctx.lineTo(p2.x, p2.y); ctx.stroke();
      }
    }

    /* 坐标轴 */
    var o = ORIGIN;
    var L = b.radius * 1.05;
    var axes = [
      { to: [o[0] + L, o[1], o[2]], col: C.cyan, label: 'X' },
      { to: [o[0], o[1] + L * 0.85, o[2]], col: C.amber, label: 'Y' },
      { to: [o[0], o[1], o[2] + L], col: C.green, label: 'Z' }
    ];
    var p0 = project(o);
    axes.forEach(function (ax) {
      var p1 = project(ax.to);
      if (!p0 || !p1) return;
      ctx.strokeStyle = rgba(ax.col, 0.30);
      ctx.lineWidth = 1;
      ctx.setLineDash([4, 5]);
      ctx.beginPath(); ctx.moveTo(p0.x, p0.y); ctx.lineTo(p1.x, p1.y); ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = rgba(ax.col, 0.65);
      ctx.font = '10px ' + monoFont();
      ctx.fillText(ax.label, p1.x + 4, p1.y - 2);
    });
  }

  function monoFont() {
    return 'ui-monospace, "JetBrains Mono", Consolas, monospace';
  }

  function drawOrigin(time) {
    var p = project(ORIGIN);
    if (!p) return;
    var pulse = 0.5 + 0.5 * Math.sin(time * 2.2);

    drawGlow(p, C.cyan, 26 + pulse * 8, 0.35 + pulse * 0.2);

    ctx.strokeStyle = rgba(C.cyan, 0.75);
    ctx.lineWidth = 1.6;
    ctx.beginPath(); ctx.arc(p.x, p.y, 6, 0, TAU); ctx.stroke();

    /* 脉冲环 */
    for (var i = 0; i < 3; i++) {
      var k = ((time * 0.32 + i / 3) % 1);
      ctx.strokeStyle = rgba(C.cyan, (1 - k) * 0.38);
      ctx.lineWidth = 1.2;
      ctx.beginPath(); ctx.arc(p.x, p.y, 6 + k * 46, 0, TAU); ctx.stroke();
    }

    ctx.fillStyle = rgba(C.cyan, 0.9);
    ctx.font = '10px ' + monoFont();
    ctx.fillText('救援基点 BASE', p.x + 11, p.y + 15);
  }

  /* ---------- 航线 ---------- */
  function drawRoute(plan, route, t, active) {
    var legs = route.legs;
    if (!legs.length) return;

    for (var i = 0; i < legs.length; i++) {
      var lg = legs[i];
      var p1 = project(lg.a), p2 = project(lg.b);
      if (!p1 || !p2) continue;

      var passed = t >= lg.t1;
      var current = (t >= lg.t0 && t < lg.t1);

      if (!active) {
        /* 非当前方案：极淡的参考线 */
        ctx.strokeStyle = 'rgba(155,140,255,0.13)';
        ctx.lineWidth = 1;
        ctx.setLineDash([3, 6]);
        ctx.beginPath(); ctx.moveTo(p1.x, p1.y); ctx.lineTo(p2.x, p2.y); ctx.stroke();
        ctx.setLineDash([]);
        continue;
      }

      if (passed) {
        ctx.strokeStyle = rgba(C.cyan, 0.62);
        ctx.lineWidth = 2.2;
        ctx.setLineDash([]);
      } else if (current) {
        ctx.strokeStyle = rgba(C.amber, 0.85);
        ctx.lineWidth = 2.2;
        ctx.setLineDash([]);
      } else {
        ctx.strokeStyle = rgba(C.cyan, 0.26);
        ctx.lineWidth = 1.4;
        ctx.setLineDash([5, 7]);
      }

      /* 先铺一层柔和外发光，再画主线，画面更有层次 */
      if (passed || current) {
        ctx.save();
        ctx.globalCompositeOperation = 'lighter';
        ctx.strokeStyle = rgba(passed ? C.cyan : C.amber, 0.10);
        ctx.lineWidth = 9;
        ctx.setLineDash([]);
        ctx.beginPath(); ctx.moveTo(p1.x, p1.y); ctx.lineTo(p2.x, p2.y); ctx.stroke();
        ctx.restore();
      }

      ctx.beginPath(); ctx.moveTo(p1.x, p1.y); ctx.lineTo(p2.x, p2.y); ctx.stroke();
      ctx.setLineDash([]);

      /* 航段序号 + 距离 */
      if (active) {
        var mx = (p1.x + p2.x) / 2, my = (p1.y + p2.y) / 2;
        ctx.fillStyle = rgba(passed ? C.cyan : C.grey, passed ? 0.85 : 0.5);
        ctx.font = '9.5px ' + monoFont();
        ctx.textAlign = 'center';
        ctx.fillText((i + 1) + '·' + lg.dist.toFixed(0), mx, my - 4);
        ctx.textAlign = 'left';
      }
    }
  }

  /* ---------- 目标 ---------- */
  function statusOfTarget(id, plan, t) {
    var out = null;
    var oc = plan.outcome || [];
    for (var i = 0; i < oc.length; i++) if (oc[i].id === id) { out = oc[i]; break; }
    if (out && out.state === 'saved') {
      if (t >= out.t - 1e-6) return { state: 'saved', at: out.t };
      return { state: 'waiting' };
    }
    var life = T[id].life;
    if (t >= life - 1e-6) return { state: 'dead', at: life };
    return { state: 'waiting' };
  }

  function currentTargetId(route, t) {
    for (var i = 0; i < route.legs.length; i++) {
      var lg = route.legs[i];
      if (t >= lg.t0 - 1e-9 && t < lg.t1 - 1e-9) return lg.target;
    }
    return -1;
  }

  function drawTargets(plan, route, t, time) {
    var cur = currentTargetId(route, t);
    var list = [];

    for (var i = 0; i < T.length; i++) {
      var tg = T[i];
      var p = targetPos(tg, t);
      var pr = project(p);
      if (!pr) continue;
      list.push({ id: i, pr: pr, st: statusOfTarget(i, plan, t), cur: i === cur });
    }
    list.sort(function (a, b) { return b.pr.d - a.pr.d; });   /* 远 → 近 */

    list.forEach(function (o) {
      var st = o.st.state;
      var col = st === 'saved' ? C.green : (st === 'dead' ? C.red : (o.cur ? C.amber : C.cyan));
      var remaining = clamp(tgLifeLeft(o.id, t, st), 0, 1e9);
      var lifeMax = T[o.id].life;
      var ratio = lifeMax > 0 ? clamp(remaining / lifeMax, 0, 1) : 0;
      var r = clamp(5.5 * o.pr.s * 260, 3.2, 11);

      /* 漂移尾迹 */
      drawDriftTail(o.id, t, col);

      /* 光晕 */
      drawGlow(o.pr, col, r * (o.cur ? 4.6 : 3.4), o.cur ? 0.85 : (st === 'dead' ? 0.4 : 0.55));

      /* 生存环：剩余比例 */
      ctx.lineWidth = 1.8;
      ctx.strokeStyle = 'rgba(255,255,255,0.10)';
      ctx.beginPath(); ctx.arc(o.pr.x, o.pr.y, r + 5, 0, TAU); ctx.stroke();

      if (st !== 'dead' && st !== 'saved') {
        var ringCol = ratio > 0.45 ? C.cyan : (ratio > 0.2 ? C.amber : C.red);
        ctx.strokeStyle = rgba(ringCol, 0.95);
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.arc(o.pr.x, o.pr.y, r + 5, -Math.PI / 2, -Math.PI / 2 + TAU * ratio);
        ctx.stroke();
        if (ratio < 0.2) {
          ctx.strokeStyle = rgba(C.red, 0.25 + 0.25 * Math.sin(time * 9));
          ctx.beginPath(); ctx.arc(o.pr.x, o.pr.y, r + 9, 0, TAU); ctx.stroke();
        }
      }

      /* 中心点 */
      ctx.fillStyle = st === 'saved' ? rgba(C.green, 1)
                    : st === 'dead' ? rgba(C.red, 0.9) : '#eafcff';
      ctx.beginPath(); ctx.arc(o.pr.x, o.pr.y, r * 0.5, 0, TAU); ctx.fill();

      /* 状态图形 */
      if (st === 'saved') {
        ctx.strokeStyle = rgba(C.green, 0.95);
        ctx.lineWidth = 1.6;
        ctx.beginPath();
        ctx.moveTo(o.pr.x - 3.4, o.pr.y);
        ctx.lineTo(o.pr.x - 0.8, o.pr.y + 3);
        ctx.lineTo(o.pr.x + 4, o.pr.y - 3.4);
        ctx.stroke();
      } else if (st === 'dead') {
        ctx.strokeStyle = rgba(C.red, 0.9);
        ctx.lineWidth = 1.6;
        ctx.beginPath();
        ctx.moveTo(o.pr.x - 3, o.pr.y - 3); ctx.lineTo(o.pr.x + 3, o.pr.y + 3);
        ctx.moveTo(o.pr.x + 3, o.pr.y - 3); ctx.lineTo(o.pr.x - 3, o.pr.y + 3);
        ctx.stroke();
      } else if (o.cur) {
        /* 当前目标：旋转瞄准框 */
        var ang = time * 1.6;
        ctx.strokeStyle = rgba(C.amber, 0.9);
        ctx.lineWidth = 1.4;
        var rr = r + 13;
        for (var k = 0; k < 4; k++) {
          var a0 = ang + k * Math.PI / 2;
          ctx.beginPath();
          ctx.arc(o.pr.x, o.pr.y, rr, a0, a0 + 0.42);
          ctx.stroke();
        }
      }

      /* 标签 */
      var label = 'T' + (o.id + 1);
      ctx.font = '10px ' + monoFont();
      var tw = ctx.measureText(label).width;
      ctx.fillStyle = 'rgba(6,12,20,0.55)';
      ctx.fillRect(o.pr.x + r + 7, o.pr.y - 12, tw + 42, 14);
      ctx.fillStyle = st === 'saved' ? rgba(C.green, 0.95)
                    : st === 'dead' ? rgba(C.red, 0.9) : 'rgba(200,220,240,0.92)';
      ctx.fillText(label, o.pr.x + r + 10, o.pr.y - 2);
      ctx.fillStyle = rgba(col, 0.62);
      ctx.font = '9px ' + monoFont();
      ctx.fillText(fmt(remaining, 1) + 't', o.pr.x + r + 10 + tw + 5, o.pr.y - 2);
    });
  }

  function tgLifeLeft(id, t, st) {
    if (st === 'saved') return 0;
    if (st === 'dead') return 0;
    return T[id].life - t;
  }

  /* 目标最近一小段漂移轨迹 */
  function drawDriftTail(id, t, col) {
    var pts = [];
    var span = Math.min(6, t);
    for (var k = 4; k >= 0; k--) {
      pts.push(targetPos(T[id], t - span * k / 4));
    }
    ctx.strokeStyle = rgba(col, 0.20);
    ctx.lineWidth = 1;
    ctx.beginPath();
    var started = false;
    pts.forEach(function (p) {
      var pr = project(p);
      if (!pr) { started = false; return; }
      if (!started) { ctx.moveTo(pr.x, pr.y); started = true; }
      else ctx.lineTo(pr.x, pr.y);
    });
    ctx.stroke();
  }

  /* ---------- 飞船 ---------- */
  function shipAt(route, t) {
    var legs = route.legs;
    if (!legs.length) return { p: ORIGIN.slice(), dir: [1, 0, 0], docked: true };
    if (t <= legs[0].t0) return { p: legs[0].a.slice(), dir: sub(legs[0].b, legs[0].a), docked: t <= 0 };
    for (var i = 0; i < legs.length; i++) {
      var lg = legs[i];
      if (t >= lg.t0 && t <= lg.t1) {
        var k = lg.t1 > lg.t0 ? (t - lg.t0) / (lg.t1 - lg.t0) : 1;
        return {
          p: [lerp(lg.a[0], lg.b[0], k), lerp(lg.a[1], lg.b[1], k), lerp(lg.a[2], lg.b[2], k)],
          dir: sub(lg.b, lg.a), docked: false
        };
      }
    }
    var last = legs[legs.length - 1];
    return { p: last.b.slice(), dir: sub(last.b, last.a), docked: true };
  }

  function drawShip(route, t, time) {
    var s = shipAt(route, t);
    var pr = project(s.p);
    if (!pr) return;

    /* 尾迹 */
    var trailPts = [];
    for (var k = 10; k >= 0; k--) {
      var tt = t - k * (state.duration / 260);
      if (tt < 0) continue;
      trailPts.push(shipAt(route, tt).p);
    }
    for (var i = 1; i < trailPts.length; i++) {
      var a = project(trailPts[i - 1]), b = project(trailPts[i]);
      if (!a || !b) continue;
      var alpha = (i / trailPts.length) * 0.62;
      ctx.strokeStyle = rgba(C.cyan, alpha);
      ctx.lineWidth = 2.2 * (i / trailPts.length) + 0.4;
      ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke();
    }

    /* 朝向（屏幕空间） */
    var ahead = project(add(s.p, scale(s.dir, 0.9)));
    var ang = 0;
    if (ahead) ang = Math.atan2(ahead.y - pr.y, ahead.x - pr.x);

    drawGlow(pr, C.cyan, 26, 0.85);

    /* 引擎光焰 */
    if (!s.docked) {
      var fl = 12 + 5 * Math.sin(time * 18);
      ctx.save();
      ctx.translate(pr.x, pr.y);
      ctx.rotate(ang);
      var grad = ctx.createLinearGradient(-8, 0, -8 - fl, 0);
      grad.addColorStop(0, 'rgba(180,245,255,0.95)');
      grad.addColorStop(0.45, 'rgba(53,214,245,0.45)');
      grad.addColorStop(1, 'rgba(53,214,245,0)');
      ctx.fillStyle = grad;
      ctx.beginPath();
      ctx.moveTo(-7, -3.4); ctx.lineTo(-8 - fl, 0); ctx.lineTo(-7, 3.4);
      ctx.closePath(); ctx.fill();
      ctx.restore();
    }

    /* 船体 */
    ctx.save();
    ctx.translate(pr.x, pr.y);
    ctx.rotate(ang);
    ctx.fillStyle = '#eafcff';
    ctx.strokeStyle = rgba(C.cyan, 0.9);
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    ctx.moveTo(11, 0);
    ctx.lineTo(-6, 5.6);
    ctx.lineTo(-2.5, 0);
    ctx.lineTo(-6, -5.6);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
    ctx.restore();
  }

  /* ---------- 效果层：对接光束 / 刷新脉冲 ---------- */
  function drawEffects(route, t, time) {
    var events = D.events || [];
    for (var i = 0; i < events.length; i++) {
      var ev = events[i];
      var dt = t - ev.t;
      if (dt < 0 || dt > 2.2) continue;

      if (ev.type === 'saved' && ev.target >= 0) {
        var p = targetPos(T[ev.target], ev.t);
        var pr = project(p);
        if (!pr) continue;
        var k = dt / 2.2;
        ctx.strokeStyle = rgba(C.green, (1 - k) * 0.85);
        ctx.lineWidth = 2 * (1 - k) + 0.5;
        ctx.beginPath(); ctx.arc(pr.x, pr.y, 8 + k * 54, 0, TAU); ctx.stroke();
        ctx.strokeStyle = rgba(C.green, (1 - k) * 0.45);
        ctx.beginPath(); ctx.arc(pr.x, pr.y, 8 + k * 30, 0, TAU); ctx.stroke();
        /* 对接成功提示 */
        ctx.fillStyle = rgba(C.green, (1 - k) * 0.95);
        ctx.font = '10px ' + monoFont();
        ctx.fillText('+  T' + (ev.target + 1) + ' 已获救', pr.x + 12, pr.y - 16 - k * 16);
      } else if (ev.type === 'dead' && ev.target >= 0) {
        var p2 = targetPos(T[ev.target], ev.t);
        var pr2 = project(p2);
        if (!pr2) continue;
        var k2 = dt / 2.2;
        ctx.strokeStyle = rgba(C.red, (1 - k2) * 0.8);
        ctx.lineWidth = 2 * (1 - k2) + 0.5;
        ctx.beginPath(); ctx.arc(pr2.x, pr2.y, 6 + k2 * 40, 0, TAU); ctx.stroke();
        ctx.fillStyle = rgba(C.red, (1 - k2) * 0.95);
        ctx.font = '10px ' + monoFont();
        ctx.fillText('×  T' + (ev.target + 1) + ' 失联', pr2.x + 12, pr2.y - 16 - k2 * 16);
      } else if (ev.type === 'refresh') {
        /* 坐标刷新：从基点扩散的扫描环 */
        var o = project(ORIGIN);
        if (!o) continue;
        var k3 = dt / 2.2;
        ctx.strokeStyle = rgba(C.cyan, (1 - k3) * 0.16);
        ctx.lineWidth = 1;
        ctx.beginPath(); ctx.arc(o.x, o.y, 20 + k3 * Math.min(W, H) * 0.55, 0, TAU); ctx.stroke();
      }
    }
  }

  /* 飞船 → 当前目标 的拦截参考线（含一束淡淡的探照光） */
  function drawIntercept(route, t) {
    var cur = currentTargetId(route, t);
    if (cur < 0) return;
    var lg = null;
    for (var i = 0; i < route.legs.length; i++) {
      if (route.legs[i].target === cur) { lg = route.legs[i]; break; }
    }
    if (!lg) return;
    var s = shipAt(route, t);
    var p1 = project(s.p), p2 = project(targetPos(T[cur], t));
    if (!p1 || !p2) return;

    /* 探照光锥：以飞船为顶点、目标为中心的三角锥 */
    var dx = p2.x - p1.x, dy = p2.y - p1.y;
    var d = Math.sqrt(dx * dx + dy * dy) || 1;
    var nx = -dy / d, ny = dx / d;
    var spread = Math.min(d * 0.16, 26);
    var g = ctx.createLinearGradient(p1.x, p1.y, p2.x, p2.y);
    g.addColorStop(0, 'rgba(255,178,69,0.16)');
    g.addColorStop(0.55, 'rgba(255,178,69,0.06)');
    g.addColorStop(1, 'rgba(255,178,69,0.01)');
    ctx.fillStyle = g;
    ctx.beginPath();
    ctx.moveTo(p1.x, p1.y);
    ctx.lineTo(p2.x + nx * spread, p2.y + ny * spread);
    ctx.lineTo(p2.x - nx * spread, p2.y - ny * spread);
    ctx.closePath();
    ctx.fill();

    ctx.strokeStyle = rgba(C.amber, 0.38);
    ctx.lineWidth = 1;
    ctx.setLineDash([2, 5]);
    ctx.beginPath(); ctx.moveTo(p1.x, p1.y); ctx.lineTo(p2.x, p2.y); ctx.stroke();
    ctx.setLineDash([]);
  }

  /* =======================================================================
   *  7. 主渲染循环
   * ==================================================================== */
  var lastFrame = performance.now();
  var fpsAcc = 0, fpsN = 0, fpsShown = 0;
  var time = 0;

  function render(now) {
    var dt = Math.min((now - lastFrame) / 1000, 0.064);
    lastFrame = now;
    time += dt;

    /* FPS */
    fpsAcc += dt; fpsN++;
    if (fpsAcc > 0.5) {
      fpsShown = Math.round(fpsN / fpsAcc);
      fpsAcc = 0; fpsN = 0;
      var el = document.getElementById('fps');
      if (el) el.textContent = fpsShown + ' FPS';
    }

    /* 播放推进 */
    if (state.playing) {
      state.t += dt * BASE_RATE * state.speed;
      if (state.t >= state.duration) {
        state.t = state.duration;
        state.playing = false;
        syncPlayIcon();
        toast('任务结束 · T+' + fmt(state.duration));
      }
    }

    /* 相机自动环绕 */
    if (state.autoOrbit) state.cam.yaw += dt * 0.10;

    var plan = PLANS[state.planIdx];
    var route = ROUTES[state.planIdx];

    /* 背景渐变 */
    var bg = ctx.createRadialGradient(W * 0.5, H * 0.42, 0, W * 0.5, H * 0.5, Math.max(W, H) * 0.8);
    bg.addColorStop(0, '#0a1424');
    bg.addColorStop(0.45, '#060b14');
    bg.addColorStop(1, '#02040a');
    ctx.fillStyle = bg;
    ctx.fillRect(0, 0, W, H);

    drawNebula(time);
    drawStars(time);
    drawFloorGrid();

    if (state.showAllRoutes) {
      PLANS.forEach(function (pl, i) {
        if (i !== state.planIdx) drawRoute(pl, ROUTES[i], state.t, false);
      });
    }

    drawRoute(plan, route, state.t, true);
    drawOrigin(time);
    drawIntercept(route, state.t);
    drawTargets(plan, route, state.t, time);
    drawShip(route, state.t, time);
    drawEffects(route, state.t, time);

    updateHud();
    drawTimeline();

    requestAnimationFrame(render);
  }

  /* =======================================================================
   *  8. HUD 更新
   * ==================================================================== */
  var $ = function (id) { return document.getElementById(id); };
  /* 从 5 开始，使第一帧就触发一次列表刷新（页面刚加载、或截图工具只渲染
   * 极少帧时，列表状态也不会停留在初始值） */
  var hudAcc = 5;

  function updateHud() {
    var plan = PLANS[state.planIdx];
    var route = ROUTES[state.planIdx];
    var t = state.t;
    var cur = currentTargetId(route, t);

    $('clock').textContent = fmt(t, 2);
    $('totalTime').textContent = fmt(plan.finish, 1) + 't';
    $('progressPct').textContent = Math.round(clamp(t / state.duration, 0, 1) * 100) + '%';
    $('tlNow').textContent = 'T+' + fmt(t);
    $('tlEnd').textContent = 'T+' + fmt(state.duration);

    /* 统计 */
    var saved = 0, dead = 0, minLeft = Infinity;
    for (var i = 0; i < T.length; i++) {
      var st = statusOfTarget(i, plan, t).state;
      if (st === 'saved') saved++;
      else if (st === 'dead') dead++;
      else {
        var left = T[i].life - t;
        if (left < minLeft) minLeft = left;
      }
    }
    $('statSaved').textContent = saved;
    $('statDead').textContent = dead;
    $('statPending').textContent = T.length - saved - dead;
    $('statMinLife').textContent = minLeft === Infinity ? '--' : fmt(minLeft, 1);

    /* 目标列表（10Hz 刷新足够） */
    hudAcc += 1;
    if (hudAcc % 6 === 0) {
      refreshTargetRows(plan, t, cur);
      refreshSeq(plan, t, cur);
      refreshLog(t);
    }
  }

  /* ---------- 右侧目标列表 ---------- */
  var rowRefs = [];
  function buildTargetRows() {
    var ul = $('targetList');
    ul.innerHTML = '';
    rowRefs = [];
    T.forEach(function (tg) {
      var li = document.createElement('li');
      var ratio = tg.life > 0 ? clamp((tg.dist0 / SPEED) / tg.life, 0, 1) : 0;
      li.innerHTML =
        '<span class="tid"><i></i>' + tg.name + '</span>' +
        '<span class="life"><span class="bar"><i></i></span>' +
        '<span class="num">生存上限 ' + fmt(tg.life, 1) + 't · 直飞 ' + fmt(tg.dist0 / SPEED, 1) + 't</span></span>' +
        '<span class="st">待救援</span>';
      if (ratio > 0.6) li.classList.add('tight');
      ul.appendChild(li);
      rowRefs.push({ li: li, bar: li.querySelector('.bar > i'), st: li.querySelector('.st') });
    });
  }

  function refreshTargetRows(plan, t, cur) {
    for (var i = 0; i < T.length; i++) {
      var o = rowRefs[i];
      if (!o) continue;
      var s = statusOfTarget(i, plan, t);
      var left = clamp(T[i].life - t, 0, T[i].life);
      var ratio = T[i].life > 0 ? left / T[i].life : 0;

      o.bar.style.width = (ratio * 100).toFixed(1) + '%';
      o.li.classList.toggle('saved', s.state === 'saved');
      o.li.classList.toggle('dead', s.state === 'dead');
      o.li.classList.toggle('current', i === cur && s.state === 'waiting');

      if (s.state === 'saved')      o.st.textContent = '已获救 ' + fmt(s.at, 1) + 't';
      else if (s.state === 'dead')  o.st.textContent = '已牺牲';
      else if (i === cur)           o.st.textContent = '拦截中';
      else                          o.st.textContent = fmt(left, 1) + 't';
    }
    /* 排序：在途 → 待救援 → 已获救 → 牺牲 */
    var order = T.map(function (_, i) {
      var s = statusOfTarget(i, PLANS[state.planIdx], t).state;
      var w = i === cur ? 0 : (s === 'waiting' ? 1 : (s === 'saved' ? 2 : 3));
      return { i: i, w: w };
    }).sort(function (a, b) { return a.w - b.w || a.i - b.i; });

    var ul = $('targetList');
    order.forEach(function (o) { ul.appendChild(rowRefs[o.i].li); });
  }

  /* ---------- 救援序列 ---------- */
  var seqRefs = [];
  function buildSeq() {
    var plan = PLANS[state.planIdx];
    var ol = $('seqList');
    ol.innerHTML = '';
    seqRefs = [];
    $('seqAlgo').textContent = plan.name;

    plan.legs.forEach(function (lg, idx) {
      var li = document.createElement('li');
      li.innerHTML = '<span class="n">' + (idx + 1) + '</span>' +
                     '<span class="id">' + T[lg.to].name + '</span>' +
                     '<span class="t">' + fmt(lg.t1, 1) + 't</span>';
      ol.appendChild(li);
      seqRefs.push({ li: li, t0: lg.t0, t1: lg.t1 });
    });
    /* 牺牲目标 */
    (plan.outcome || []).forEach(function (o) {
      if (o.state === 'dead') {
        var li = document.createElement('li');
        li.className = 'dead-row';
        li.innerHTML = '<span class="n">—</span>' +
                       '<span class="id">' + T[o.id].name + '</span>' +
                       '<span class="t">牺牲</span>';
        ol.appendChild(li);
      }
    });
  }

  function refreshSeq(plan, t, cur) {
    seqRefs.forEach(function (r) {
      r.li.classList.toggle('done', t >= r.t1 - 1e-6);
      r.li.classList.toggle('current', t >= r.t0 - 1e-6 && t < r.t1 - 1e-6);
    });
  }

  /* ---------- 算法对比 ---------- */
  function buildAlgoList() {
    var ul = $('algoList');
    ul.innerHTML = '';
    var maxLen = 1, maxNode = 1, maxTime = 1;
    PLANS.forEach(function (p) {
      maxLen = Math.max(maxLen, p.length);
      maxNode = Math.max(maxNode, p.nodes || 1);
      maxTime = Math.max(maxTime, p.elapsedMs || 0.001);
    });

    PLANS.forEach(function (p, i) {
      var li = document.createElement('li');
      if (i === state.planIdx) li.classList.add('sel');
      var star = (i === META.selectedPlan) ? '★ ' : '';
      var badge = p.feasible
        ? '<span class="badge">全部获救</span>'
        : '<span class="badge bad">牺牲 ' + p.dead + ' 个</span>';

      li.innerHTML =
        '<div class="algo-head">' + star + p.name + badge + '</div>' +
        '<div class="algo-metrics">' +
          '<div>总航程 <b>' + fmt(p.length, 1) + '</b></div>' +
          '<div>完成 <b>' + fmt(p.finish, 1) + 't</b></div>' +
          '<div>搜索节点 <b>' + (p.nodes || 0) + '</b></div>' +
          '<div>耗时 <b>' + fmt(p.elapsedMs || 0, 3) + 'ms</b></div>' +
        '</div>' +
        '<div class="algo-bars">' +
          '<div class="row"><i class="len" style="width:' + (p.length / maxLen * 100) + '%"></i></div>' +
          '<div class="row"><i class="node" style="width:' + ((p.nodes || 1) / maxNode * 100) + '%"></i></div>' +
          '<div class="row"><i class="time" style="width:' + ((p.elapsedMs || 0.001) / maxTime * 100) + '%"></i></div>' +
        '</div>';

      li.addEventListener('click', function () { selectPlan(i); });
      ul.appendChild(li);
    });
  }

  function selectPlan(i) {
    if (i === state.planIdx) return;
    state.planIdx = i;
    state.t = Math.min(state.t, state.duration);
    buildSeq();
    buildAlgoList();
    buildLog();
    toast('已切换到：' + PLANS[i].name);
  }

  /* ---------- 事件日志 ---------- */
  var logRefs = [];
  var logAll = true;
  function buildLog() {
    var ul = $('logList');
    ul.innerHTML = '';
    logRefs = [];

    /* 合并所有方案相关事件：以当前方案重算 */
    var plan = PLANS[state.planIdx];
    var evs = [];
    evs.push({ t: 0, type: 'launch', target: -1, text: '救援飞船离开发射基点' });
    plan.legs.forEach(function (lg) {
      evs.push({ t: lg.t1, type: 'saved', target: lg.to, text: T[lg.to].name + ' 完成对接' });
    });
    (plan.outcome || []).forEach(function (o) {
      if (o.state === 'dead') evs.push({ t: o.t, type: 'dead', target: o.id, text: T[o.id].name + ' 生存时间耗尽' });
    });
    Array.prototype.push.apply(evs, (D.events || []).filter(function (e) { return e.type === 'refresh'; }));
    evs.push({ t: plan.finish, type: 'finish', target: -1, text: '救援任务结束' });
    evs.sort(function (a, b) { return a.t - b.t; });

    evs.forEach(function (e) {
      var li = document.createElement('li');
      li.className = e.type;
      li.innerHTML = '<span class="tt">' + fmt(e.t, 1) + 't</span><span>' + e.text + '</span>';
      li.addEventListener('click', function () { seek(e.t); });
      ul.appendChild(li);
      logRefs.push({ li: li, e: e });
    });
    applyLogFilter();
  }

  function applyLogFilter() {
    logRefs.forEach(function (r) {
      r.li.style.display = (logAll || r.e.type !== 'refresh') ? '' : 'none';
    });
  }

  var lastNear = -1;
  function refreshLog(t) {
    var near = -1, best = 1e9;
    for (var i = 0; i < logRefs.length; i++) {
      var d = Math.abs(logRefs[i].e.t - t);
      if (d < best) { best = d; near = i; }
    }
    if (near === lastNear) return;
    if (lastNear >= 0 && logRefs[lastNear]) logRefs[lastNear].li.classList.remove('near');
    if (near >= 0 && best < state.duration * 0.04) {
      logRefs[near].li.classList.add('near');
      lastNear = near;
      var el = logRefs[near].li;
      var box = el.parentElement;
      if (box && el.offsetTop < box.scrollTop + 20) box.scrollTop = el.offsetTop - 40;
    } else {
      lastNear = -1;
    }
  }

  /* =======================================================================
   *  9. 时间轴
   * ==================================================================== */
  var tl = document.getElementById('tlCanvas');
  var tlx = tl.getContext('2d');
  var TLW = 0, TLH = 0;

  function resizeTL() {
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    TLW = tl.clientWidth;
    TLH = tl.clientHeight;
    tl.width = Math.round(TLW * dpr);
    tl.height = Math.round(TLH * dpr);
    tlx.setTransform(dpr, 0, 0, dpr, 0, 0);
  }
  window.addEventListener('resize', resizeTL);

  function tlX(t) {
    var pad = 6;
    return pad + (TLW - pad * 2) * clamp(t / state.duration, 0, 1);
  }
  function tlT(x) {
    var pad = 6;
    return clamp((x - pad) / (TLW - pad * 2), 0, 1) * state.duration;
  }

  function drawTimeline() {
    if (!TLW) return;
    var plan = PLANS[state.planIdx];
    tlx.clearRect(0, 0, TLW, TLH);

    var y = TLH * 0.52;

    /* 底轨 */
    tlx.strokeStyle = 'rgba(120,170,220,0.16)';
    tlx.lineWidth = 3;
    tlx.lineCap = 'round';
    tlx.beginPath(); tlx.moveTo(6, y); tlx.lineTo(TLW - 6, y); tlx.stroke();

    /* 已播进度 */
    tlx.strokeStyle = rgba(C.cyan, 0.75);
    tlx.lineWidth = 3;
    tlx.beginPath(); tlx.moveTo(6, y); tlx.lineTo(tlX(state.t), y); tlx.stroke();

    /* 事件刻度 */
    var evs = [];
    plan.legs.forEach(function (lg) { evs.push({ t: lg.t1, type: 'saved' }); });
    (plan.outcome || []).forEach(function (o) { if (o.state === 'dead') evs.push({ t: o.t, type: 'dead' }); });

    evs.forEach(function (e) {
      var x = tlX(e.t);
      var col = e.type === 'saved' ? C.green : C.red;
      tlx.strokeStyle = rgba(col, 0.85);
      tlx.lineWidth = 1.4;
      tlx.beginPath();
      tlx.moveTo(x, y - 11); tlx.lineTo(x, y - 4);
      tlx.stroke();
      tlx.fillStyle = rgba(col, 0.9);
      tlx.beginPath(); tlx.arc(x, y - 12.5, 2, 0, TAU); tlx.fill();
    });

    /* 坐标刷新刻度（下方） */
    var r = REFRESH;
    for (var tt = r; tt < state.duration; tt += r) {
      var xr = tlX(tt);
      tlx.strokeStyle = 'rgba(120,170,220,0.30)';
      tlx.lineWidth = 1;
      tlx.beginPath(); tlx.moveTo(xr, y + 5); tlx.lineTo(xr, y + 9); tlx.stroke();
    }

    /* 播放头 */
    var px = tlX(state.t);
    tlx.strokeStyle = '#eafcff';
    tlx.lineWidth = 1.6;
    tlx.beginPath(); tlx.moveTo(px, y - 15); tlx.lineTo(px, y + 13); tlx.stroke();
    tlx.fillStyle = '#eafcff';
    tlx.beginPath(); tlx.arc(px, y, 3.6, 0, TAU); tlx.fill();

    /* 悬停提示 */
    if (state.hoverT !== null) {
      var hx = tlX(state.hoverT);
      tlx.strokeStyle = 'rgba(255,178,69,0.6)';
      tlx.setLineDash([2, 3]);
      tlx.beginPath(); tlx.moveTo(hx, 2); tlx.lineTo(hx, TLH - 2); tlx.stroke();
      tlx.setLineDash([]);
      tlx.fillStyle = 'rgba(255,178,69,0.95)';
      tlx.font = '9.5px ' + monoFont();
      tlx.fillText('T+' + fmt(state.hoverT, 1), clamp(hx - 18, 2, TLW - 46), 10);
    }
  }

  var dragging = false;
  function seek(t) {
    state.t = clamp(t, 0, state.duration);
  }

  tl.addEventListener('pointerdown', function (e) {
    dragging = true;
    tl.setPointerCapture(e.pointerId);
    var r = tl.getBoundingClientRect();
    seek(tlT(e.clientX - r.left));
    state.playing = false; syncPlayIcon();
  });
  tl.addEventListener('pointermove', function (e) {
    var r = tl.getBoundingClientRect();
    var t = tlT(e.clientX - r.left);
    state.hoverT = t;
    if (dragging) seek(t);
  });
  tl.addEventListener('pointerup', function () { dragging = false; });
  tl.addEventListener('pointerleave', function () { state.hoverT = null; });

  /* =======================================================================
   * 10. 交互
   * ==================================================================== */
  function syncPlayIcon() {
    $('playIcon').setAttribute('d', state.playing
      ? 'M7 5h4v14H7zM13 5h4v14h-4z'
      : 'M8 5v14l11-7z');
  }

  $('btnPlay').addEventListener('click', function () {
    if (state.t >= state.duration - 1e-6) state.t = 0;
    state.playing = !state.playing;
    syncPlayIcon();
  });

  /* 重置：把演示恢复到“刚打开”的状态
   *   时间归零 + 暂停、速度回 1×、方案回到推荐方案、相机回默认视角、
   *   关掉“自动环绕/俯视/全部方案航线”这类临时开关、收起遮罩。 */
  function resetAll() {
    var defPlan = clamp(META.selectedPlan || 0, 0, PLANS.length - 1);

    state.t = 0;
    state.playing = false;
    state.speed = 1;
    state.hoverT = null;
    state.autoOrbit = true;
    state.showAllRoutes = false;

    var base = state.bounds.radius * 3.1;
    state.cam.yaw = 0.72;
    state.cam.pitch = 0.40;
    state.cam.dist = base;
    VSCALE = 0.40 * Math.min(W, H) * base / state.bounds.radius;

    if (state.planIdx !== defPlan) {
      state.planIdx = defPlan;
      buildSeq();
      buildAlgoList();
      buildLog();
    }

    document.querySelectorAll('#speedGroup button').forEach(function (x) {
      x.classList.toggle('active', parseFloat(x.dataset.speed) === 1);
    });
    $('btnOrbit').classList.add('active');
    $('btnChain').classList.remove('active');

    showBriefing(false);
    showAbout(false);

    syncPlayIcon();
    toast('已重置：回到起点 · 1× 速度 · 推荐方案');
  }

  $('btnReset').addEventListener('click', resetAll);
  $('btnResetTop').addEventListener('click', resetAll);
  document.querySelectorAll('#speedGroup button').forEach(function (b) {
    b.addEventListener('click', function () {
      document.querySelectorAll('#speedGroup button').forEach(function (x) { x.classList.remove('active'); });
      b.classList.add('active');
      state.speed = parseFloat(b.dataset.speed);
    });
  });
  $('btnOrbit').addEventListener('click', function () {
    state.autoOrbit = !state.autoOrbit;
    this.classList.toggle('active', state.autoOrbit);
  });
  $('btnOrbit').classList.toggle('active', state.autoOrbit);

  $('btnTop').addEventListener('click', function () {
    state.cam.pitch = 1.42;       /* 接近俯视 */
    state.cam.yaw = 0;
    state.autoOrbit = false;
    $('btnOrbit').classList.remove('active');
  });
  $('btnChain').addEventListener('click', function () {
    state.showAllRoutes = !state.showAllRoutes;
    this.classList.toggle('active', state.showAllRoutes);
  });

  /* 相机拖拽 / 缩放 */
  var drag = null;
  cv.addEventListener('pointerdown', function (e) {
    drag = { x: e.clientX, y: e.clientY, yaw: state.cam.yaw, pitch: state.cam.pitch };
    cv.setPointerCapture(e.pointerId);
  });
  cv.addEventListener('pointermove', function (e) {
    if (!drag) return;
    state.cam.yaw = drag.yaw + (e.clientX - drag.x) * 0.006;
    state.cam.pitch = clamp(drag.pitch - (e.clientY - drag.y) * 0.005, -1.45, 1.45);
  });
  cv.addEventListener('pointerup', function () { drag = null; });
  cv.addEventListener('pointercancel', function () { drag = null; });
  cv.addEventListener('wheel', function (e) {
    e.preventDefault();
    var k = Math.exp(e.deltaY * 0.0012);
    var base = state.bounds.radius * 3.1;
    state.cam.dist = clamp(state.cam.dist * k, base * 0.35, base * 2.6);
    VSCALE = 0.40 * Math.min(W, H) * base / state.bounds.radius;
  }, { passive: false });

  cv.addEventListener('dblclick', function () {
    state.cam.yaw = 0.72;
    state.cam.pitch = 0.40;
    state.cam.dist = state.bounds.radius * 3.1;
    VSCALE = 0.40 * Math.min(W, H) * state.cam.dist / state.bounds.radius;
  });

  /* tabs */
  document.querySelectorAll('#tabs button').forEach(function (b) {
    b.addEventListener('click', function () {
      document.querySelectorAll('#tabs button').forEach(function (x) { x.classList.remove('active'); });
      b.classList.add('active');
      document.querySelectorAll('.tab-body').forEach(function (s) {
        s.classList.toggle('hidden', s.dataset.body !== b.dataset.tab);
      });
    });
  });

  $('logFilterAll').addEventListener('change', function () {
    logAll = this.checked;
    applyLogFilter();
  });

  /* 键盘 */
  window.addEventListener('keydown', function (e) {
    if (e.target.tagName === 'INPUT') return;
    if (e.key === 'Escape') {
      /* Esc 先关“关于”，再关“简报” */
      if (about && !about.classList.contains('hidden')) { showAbout(false); return; }
      if (overlay && !overlay.classList.contains('hidden')) { showBriefing(false); }
      return;
    }
    if (e.code === 'Space') {
      e.preventDefault();
      if (state.t >= state.duration - 1e-6) state.t = 0;
      state.playing = !state.playing; syncPlayIcon();
    } else if (e.code === 'ArrowRight') {
      seek(state.t + 0.5); state.playing = false; syncPlayIcon();
    } else if (e.code === 'ArrowLeft') {
      seek(state.t - 0.5); state.playing = false; syncPlayIcon();
    } else if (e.key === 'r' || e.key === 'R') {
      resetAll();
    } else if (e.key === 'c' || e.key === 'C') {
      state.autoOrbit = !state.autoOrbit;
      $('btnOrbit').classList.toggle('active', state.autoOrbit);
    }
  });

  /* 折叠面板 */
  document.querySelectorAll('.collapse').forEach(function (b) {
    b.addEventListener('click', function () {
      var p = document.getElementById(b.dataset.target);
      var hidden = p.style.display === 'none';
      p.style.display = hidden ? '' : 'none';
      b.textContent = hidden ? '–' : '+';
    });
  });

  /* 导出 PNG */
  $('btnShot').addEventListener('click', function () {
    var a = document.createElement('a');
    a.download = 'rescue_T' + fmt(state.t, 1).replace('.', '_') + '.png';
    a.href = cv.toDataURL('image/png');
    a.click();
    toast('已导出当前画面');
  });

  /* 两个遮罩：简报 / 关于。同一时间只允许显示一个，避免叠在一起看不清。 */
  var overlay = $('overlay');
  var about = $('aboutOverlay');

  function setOverlay(el, show) {
    if (!el) return;
    el.classList.toggle('hidden', !show);
    el.setAttribute('aria-hidden', show ? 'false' : 'true');
  }

  function showBriefing(show) {
    setOverlay(overlay, show);
    if (show) setOverlay(about, false);
  }

  function showAbout(show) {
    setOverlay(about, show);
    if (!show) return;
    setOverlay(overlay, false);
    var src = $('aboutSource');
    if (src) {
      src.textContent = META.dataSource === 'mock'
        ? 'C 程序导出（测试桩）'
        : 'C 程序导出（正式模块）';
    }
    var body = about.querySelector('.about-body');
    if (body) body.scrollTop = 0;
    var close = $('btnCloseAbout');
    if (close) close.focus();
  }

  $('btnBriefing').addEventListener('click', function () { showBriefing(true); });
  $('btnCloseBriefing').addEventListener('click', function () { showBriefing(false); });
  overlay.addEventListener('click', function (e) { if (e.target === overlay) showBriefing(false); });
  $('btnStart').addEventListener('click', function () {
    showBriefing(false);
    state.t = 0;
    state.playing = true;
    syncPlayIcon();
  });

  $('btnAbout').addEventListener('click', function () { showAbout(true); });
  $('btnCloseAbout').addEventListener('click', function () { showAbout(false); });
  $('btnCloseAbout2').addEventListener('click', function () { showAbout(false); });
  $('btnAboutRepo').addEventListener('click', function () {
    window.open('https://github.com/NH123-ai/space-rescue', '_blank', 'noopener');
  });
  about.addEventListener('click', function (e) { if (e.target === about) showAbout(false); });

  /* toast */
  var toastTimer = null;
  function toast(msg) {
    var t = $('toast');
    t.textContent = msg;
    t.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { t.classList.remove('show'); }, 1800);
  }

  /* =======================================================================
   * 11. 初始化
   * ==================================================================== */
  /* 调试钩子：投影函数、状态、当前航线（便于答辩现场用控制台定位元素，
   * 也供自动化自检脚本取样验证“目标确实画在了预期屏幕位置”） */
  window.__RESCUE_DEBUG = {
    project: project,
    state: state,
    targetPos: targetPos,
    shipAt: shipAt,
    targets: T,
    plans: PLANS,
    routeNow: function () { return ROUTES[state.planIdx]; }
  };

  function fillMeta() {
    var chips = $('metaChips');
    var plan = PLANS[state.planIdx];
    var isMock = (META.dataSource === 'mock');
    var items = [
      { k: '场景', v: META.scenarioName || ('#' + (META.scenario || 0)) },
      { k: '目标', v: META.count + ' 个' },
      { k: '飞船速度', v: fmt(SPEED, 1) },
      { k: '刷新间隔', v: fmt(REFRESH, 1) + 't' },
      { k: '种子', v: META.seed || 0 },
      { k: '方案', v: plan.name },
      { k: '结果', v: plan.saved + '/' + META.count + ' 获救', warn: !plan.feasible },
      isMock ? { k: '数据', v: '测试桩（等队友模块接入）', warn: true }
             : { k: '数据', v: '正式模块' }
    ];
    chips.innerHTML = items.map(function (it) {
      return '<span class="chip' + (it.warn ? ' warn' : '') + '">' + it.k +
             ' <b>' + it.v + '</b></span>';
    }).join('');

    $('bCount').textContent = META.count;
    $('bSpeed').textContent = fmt(SPEED, 1);
    $('bInterval').textContent = fmt(REFRESH, 1);

    var stats = [
      { v: META.count, k: '待救援目标' },
      { v: plan.saved + '/' + META.count, k: '推荐方案获救' },
      { v: fmt(plan.finish, 1) + 't', k: '总救援耗时' },
      { v: fmt(plan.length, 1), k: '总航程' },
      { v: fmt(state.duration, 1) + 't', k: '演示时长' }
    ];
    $('briefingStats').innerHTML = stats.map(function (s) {
      return '<div class="bs"><b>' + s.v + '</b><span>' + s.k + '</span></div>';
    }).join('');

    $('bSource').textContent = 'web/trace.js · 生成于 ' + (META.generated || '—');
    if (META.dataSource === 'mock') {
      $('bSource').textContent = '测试台数据（mock）· 生成于 ' + (META.generated || '—');
    }
  }

  /* ---------- 场景切换（公开演示站用：?trace=<key>）---------- */
  function setupScenePicker() {
    var sel = $('sceneSelect');
    var scene = window.__RESCUE_SCENE || {};
    var cat = scene.catalog || [];

    if (!sel) return;
    if (cat.length < 2) {                     // 只有一份数据：隐藏选择器
      var box = sel.parentNode;
      if (box && box.parentNode) box.parentNode.removeChild(box);
      return;
    }

    sel.innerHTML = cat.map(function (c) {
      return '<option value="' + c.key + '"' +
             (c.key === scene.key ? ' selected' : '') + '>' + c.title + '</option>';
    }).join('');

    sel.addEventListener('change', function () {
      var u = new URL(location.href);
      u.searchParams.set('trace', sel.value);
      u.searchParams.delete('t');             // 换场景后从头播放
      location.href = u.toString();
    });

    /* 标题带上场景名，分享出去的链接更容易看懂 */
    if (scene.title) {
      document.title = scene.title + ' · 太空救援任务 3D 演示';
    }
    var src = $('bSource');
    if (src) {
      src.textContent = 'traces/' + (scene.key || '') + '/trace.js · 生成于 ' +
                        (META.generated || '—') +
                        (META.dataSource === 'mock' ? '（测试桩数据）' : '');
    }
    var descEl = document.getElementById('bSceneDesc');
    if (descEl && scene.desc) descEl.textContent = scene.desc;
  }

  function init() {
    /* 可选 URL 参数，便于答辩时直接定位到某个场景：
     *   ?t=42       起始时刻
     *   ?plan=1     起始方案下标
     *   ?brief=0    不显示任务简报遮罩
     *   ?about=1    直接打开“关于”面板（项目介绍）
     *   ?orbit=0    关闭自动环绕
     *   ?play=1     自动播放 */
    var q = new URLSearchParams(location.search);
    if (q.has('plan')) state.planIdx = clamp(parseInt(q.get('plan'), 10) || 0, 0, PLANS.length - 1);
    if (q.has('t')) state.t = clamp(parseFloat(q.get('t')) || 0, 0, state.duration);
    if (q.get('brief') === '0') overlay.classList.add('hidden');
    if (q.get('orbit') === '0') {
      state.autoOrbit = false;
      $('btnOrbit').classList.remove('active');
    }
    if (q.get('play') === '1') { state.playing = true; }
    if (q.has('pitch')) state.cam.pitch = clamp(parseFloat(q.get('pitch')) || 0.4, -1.45, 1.45);
    if (q.has('yaw')) state.cam.yaw = parseFloat(q.get('yaw')) || 0;

    resize();
    resizeTL();
    buildTargetRows();
    buildSeq();
    buildAlgoList();
    buildLog();
    fillMeta();
    setupScenePicker();
    syncPlayIcon();
    /* ?about=1 直接打开“关于”面板（方便把“项目介绍”当入口分享出去） */
    if (q.get('about') === '1') showAbout(true);
    requestAnimationFrame(function (t0) { lastFrame = t0; render(t0); });
  }

  init();
})();
