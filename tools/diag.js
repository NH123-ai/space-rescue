/* 自动化自检脚本（仅用于 _selfcheck.html，不参与发布）：
 * 1) 收集 JS 错误
 * 2) 统计主画布/时间轴的像素分布，判断"确实画了东西"
 * 3) 用 __RESCUE_DEBUG.project 计算每个目标/飞船的屏幕坐标，
 *    再去画布上采样该处像素，验证"目标画在了正确的位置"
 * 4) 检查面板与时间轴是否重叠、是否超出视口
 */
(function () {
  'use strict';
  var errs = [];
  window.addEventListener('error', function (e) { errs.push(String(e.message || e.error)); });
  window.addEventListener('unhandledrejection', function (e) { errs.push('promise:' + e.reason); });

  function statCanvas(cv) {
    if (!cv) return null;
    var ctx = cv.getContext('2d');
    var w = cv.width, h = cv.height;
    if (!w || !h) return { w: 0, h: 0 };
    var d = ctx.getImageData(0, 0, w, h).data;
    var n = 0, bright = 0, colored = 0, sum = [0, 0, 0];
    var bins = {};
    for (var i = 0; i < d.length; i += 16) {
      var r = d[i], g = d[i + 1], b = d[i + 2];
      n++; sum[0] += r; sum[1] += g; sum[2] += b;
      if (r + g + b > 170) bright++;
      if (Math.max(r, g, b) - Math.min(r, g, b) > 45) colored++;
      bins[(r >> 4) + ',' + (g >> 4) + ',' + (b >> 4)] = 1;
    }
    return {
      w: w, h: h, px: n,
      brightPct: +(100 * bright / n).toFixed(2),
      coloredPct: +(100 * colored / n).toFixed(2),
      distinctBins: Object.keys(bins).length,
      meanRGB: sum.map(function (v) { return Math.round(v / n); })
    };
  }

  /* 在 (px,py) 附近 size×size 的框内统计"亮像素/彩色像素"，并按主色分类 */
  function sampleAround(cv, px, py, size) {
    var ctx = cv.getContext('2d');
    var dpr = cv.width / cv.clientWidth;
    var half = (size * dpr) / 2;
    var x0 = Math.round(px * dpr - half), y0 = Math.round(py * dpr - half);
    var s = Math.round(size * dpr);
    x0 = Math.max(0, Math.min(cv.width - s, x0));
    y0 = Math.max(0, Math.min(cv.height - s, y0));
    if (s <= 0 || x0 + s > cv.width || y0 + s > cv.height) return null;
    var d = ctx.getImageData(x0, y0, s, s).data;
    var n = 0, bright = 0, cyan = 0, green = 0, red = 0, amber = 0, white = 0;
    for (var i = 0; i < d.length; i += 4) {
      var r = d[i], g = d[i + 1], b = d[i + 2];
      n++;
      if (r + g + b > 170) bright++;
      if (g > 120 && b > 140 && r < 130) cyan++;
      else if (g > 150 && r < 140 && b < 190) green++;
      else if (r > 150 && g < 130 && b < 150) red++;
      else if (r > 170 && g > 120 && b < 120) amber++;
      else if (r > 180 && g > 180 && b > 180) white++;
    }
    return {
      brightPct: +(100 * bright / n).toFixed(1),
      cyan: cyan, green: green, red: red, amber: amber, white: white
    };
  }

  function rect(sel) {
    var el = document.querySelector(sel); if (!el) return null;
    var r = el.getBoundingClientRect(), cs = getComputedStyle(el);
    return {
      x: Math.round(r.x), y: Math.round(r.y), w: Math.round(r.width), h: Math.round(r.height),
      display: cs.display, visible: cs.display !== 'none' && r.width > 0 && r.height > 0,
      bottom: Math.round(r.bottom), right: Math.round(r.right)
    };
  }

  function overlaps(a, b) {
    if (!a || !b || !a.visible || !b.visible) return false;
    return !(a.right <= b.x || b.right <= a.x || a.bottom <= b.y || b.bottom <= a.y);
  }

  setTimeout(function () {
    var out = { errs: errs, hasTrace: !!window.RESCUE_TRACE };
    var cv = document.getElementById('stage');
    out.stage = statCanvas(cv);
    out.timeline = statCanvas(document.getElementById('tlCanvas'));

    var R = { topbar: rect('.topbar'), left: rect('.panel-left'), right: rect('.panel-right'),
              timeline: rect('.timeline'), overlay: rect('.overlay') };
    out.rects = R;

    var vw = innerWidth, vh = innerHeight;
    out.layout = {
      viewport: [vw, vh],
      centerGapPx: Math.round(R.right.x - R.left.right),
      panelLeftOverlapTimeline: overlaps(R.left, R.timeline),
      panelRightOverlapTimeline: overlaps(R.right, R.timeline),
      leftOutOfView: R.left ? (R.left.x < 0 || R.left.bottom > vh) : null,
      rightOutOfView: R.right ? (R.right.right > vw || R.right.bottom > vh) : null,
      timelineOutOfView: R.timeline ? (R.timeline.x < 0 || R.timeline.right > vw + 1) : null
    };

    /* 逐目标采样：验证 3D 场景里确实在这些屏幕位置画出了目标点 */
    var S = window.__RESCUE_DEBUG;
    if (S && cv) {
      var t = S.state.t;
      out.samples = [];
      for (var i = 0; i < S.targets.length; i++) {
        var p = S.targetPos(S.targets[i], t);
        var pr = S.project(p);
        if (!pr) { out.samples.push({ id: i, offscreen: true }); continue; }
        var inView = pr.x > 0 && pr.x < cv.clientWidth && pr.y > 0 && pr.y < cv.clientHeight;
        var sm = inView ? sampleAround(cv, pr.x, pr.y, 26) : null;
        out.samples.push({ id: i, x: Math.round(pr.x), y: Math.round(pr.y),
                           inView: inView, sm: sm });
      }
      var ship = S.shipAt(S.routeNow(), t);
      var sp = S.project(ship.p);
      out.ship = sp ? { x: Math.round(sp.x), y: Math.round(sp.y),
                        sm: sampleAround(cv, sp.x, sp.y, 32) } : null;
      out.camera = { yaw: +S.state.cam.yaw.toFixed(3), pitch: +S.state.cam.pitch.toFixed(3),
                     dist: Math.round(S.state.cam.dist) };
    }

    out.overlayHidden = document.getElementById('overlay').classList.contains('hidden');
    out.text = {
      clock: (document.getElementById('clock') || {}).textContent,
      statSaved: (document.getElementById('statSaved') || {}).textContent,
      statDead: (document.getElementById('statDead') || {}).textContent,
      statPending: (document.getElementById('statPending') || {}).textContent,
      minLife: (document.getElementById('statMinLife') || {}).textContent,
      seqAlgo: (document.getElementById('seqAlgo') || {}).textContent,
      totalTime: (document.getElementById('totalTime') || {}).textContent,
      progress: (document.getElementById('progressPct') || {}).textContent
    };
    out.counts = {
      targets: document.querySelectorAll('#targetList li').length,
      seq: document.querySelectorAll('#seqList li').length,
      algos: document.querySelectorAll('#algoList li').length,
      logs: document.querySelectorAll('#logList li').length
    };
    out.targetRowStates = [].map.call(document.querySelectorAll('#targetList li'), function (li) {
      return li.className || 'default';
    });

    /* ---- 数据契约自检 ----
     * trace 里只给了“初始坐标 + 速度 + 各航段时刻”，前端按解析规律还原轨迹。
     * 这里反过来校验：用 p0+v·t 推出的航段长度是否与 legs[].dist 一致。
     * 若不一致，说明 C 端导出的不是 t=0 的初始坐标（历史上踩过这个坑）。 */
    var TR = window.RESCUE_TRACE;
    if (TR) {
      var bad = [], checked = 0, maxErr = 0;
      var O = (TR.meta && TR.meta.origin) || [0, 0, 0];
      TR.plans.forEach(function (pl) {
        var posAt = function (id, t) {
          var tg = TR.targets[id];
          return [tg.p0[0] + tg.v[0] * t, tg.p0[1] + tg.v[1] * t, tg.p0[2] + tg.v[2] * t];
        };
        (pl.legs || []).forEach(function (lg) {
          var start = (lg.from < 0) ? O : posAt(lg.from, lg.t0);
          var end = posAt(lg.to, lg.t1);
          var dx = end[0] - start[0], dy = end[1] - start[1], dz = end[2] - start[2];
          var d = Math.sqrt(dx * dx + dy * dy + dz * dz);
          var err = Math.abs(d - lg.dist);
          checked++;
          if (err > Math.max(0.05, lg.dist * 0.002)) bad.push({ plan: pl.id, to: lg.to, d: d, dist: lg.dist });
          if (err > maxErr) maxErr = err;
        });
      });
      out.contract = { checked: checked, mismatches: bad.length, maxErr: +maxErr.toFixed(4), bad: bad.slice(0, 5) };
    }

    var el = document.getElementById('__diag');
    el.textContent = 'DIAG_START' + JSON.stringify(out) + 'DIAG_END';
  }, 1700);
})();
