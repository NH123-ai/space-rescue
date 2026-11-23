/*============================================================================
 *  boot.js  —  数据加载器（决定“这次玩哪个场景”，再把渲染代码拉起来）
 *----------------------------------------------------------------------------
 *  为什么要单独一层：
 *    公开演示站上数据是预先生成好的（静态托管没有后端），一个场景一份
 *    traces/<key>/trace.js。页面通过 ?trace=<key> 选择场景，这里负责：
 *      1) 读 URL 参数，查 traces/catalog.js 里的清单找到对应数据文件；
 *      2) 动态插入 <script> 加载数据（file:// 下也能用，不受 fetch 跨域限制）；
 *      3) 数据就绪后再加载 app.js —— 保证 app.js 读到的 window.RESCUE_TRACE
 *         一定是最终那份；
 *      4) 加载失败时给出中文提示，而不是白屏。
 *
 *  兼容两种老用法：
 *    · 页面上已经用 <script src="trace.js"> 预先放好数据（例如 viz_test/preview.html）
 *      → 这里直接加载 app.js，不再覆盖；
 *    · 只有 web/trace.js、没有 catalog.js 的旧部署 → 回退到 trace.js。
 *===========================================================================*/
(function () {
  'use strict';

  var params = new URLSearchParams(location.search);

  /* app.js 与 boot.js 放在同一目录：用 boot.js 自己的 URL 推出目录，
   * 这样 viz_test/preview.html 通过 ../web/boot.js 引用时也能正确找到 ../web/app.js */
  var BASE = '';
  (function () {
    var cs = document.currentScript;
    var src = cs && cs.src;
    if (!src) {
      var all = document.getElementsByTagName('script');
      for (var i = all.length - 1; i >= 0; --i) {
        if (/boot\.js(\?|$)/.test(all[i].src)) { src = all[i].src; break; }
      }
    }
    if (src) BASE = src.replace(/[?#].*$/, '').replace(/[^/]*$/, '');
  })();

  /* 场景选择器上要显示的当前场景信息，供 app.js 使用 */
  var scene = { key: null, title: null, desc: null, catalog: [] };

  function loadApp() {
    var s = document.createElement('script');
    s.src = BASE + 'app.js';
    s.onerror = function () {
      showError('渲染脚本 app.js 加载失败：' + BASE + 'app.js');
    };
    document.body.appendChild(s);
  }

  function showError(msg) {
    var box = document.createElement('div');
    box.style.cssText =
      'position:fixed;inset:0;z-index:99;display:grid;place-items:center;' +
      'background:radial-gradient(120% 90% at 50% 0%,#0b1524,#02040a);' +
      'color:#a9b8cf;font:14px/1.9 system-ui,"Microsoft YaHei",sans-serif;text-align:center;padding:24px';
    box.innerHTML =
      '<div><h2 style="color:#35d6f5;letter-spacing:.08em;margin:0 0 12px">数据没加载成功</h2>' +
      '<p style="max-width:620px;margin:0 auto 10px">' + msg + '</p>' +
      '<p style="color:#6c7c95">排查顺序：<br>' +
      '1) 先跑一次 <code style="color:#ffb245">space_rescue.exe --trace-all</code> 生成 web\\traces\\ 下的数据；<br>' +
      '2) 确认 web\\traces\\catalog.js 存在；<br>' +
      '3) 直接打开 web\\index.html（不要用编辑器预览插件）。</p></div>';
    document.body.appendChild(box);
  }

  /* ---- 情况 1：页面上已经预先加载了数据（preview.html 等）---- */
  if (window.RESCUE_TRACE) {
    loadApp();
    return;
  }

  /* ---- 情况 2：?rand=1 —— 用「随机」按钮现生成的那一局（存在 sessionStorage）---- */
  if (params.get('rand') === '1') {
    var raw = null;
    try { raw = sessionStorage.getItem('rescue:rand'); } catch (e) { raw = null; }
    if (raw) {
      try {
        var tr = JSON.parse(raw);
        window.RESCUE_TRACE = tr;
        window.__RESCUE_SCENE = {
          /* 用和目录里场景不同的 key，避免和清单里的 “random” 预设撞名 */
          key: 'local-random',
          title: '本机随机场景',
          desc: '按「随机」现场生成：位置、漂移速度、生存时间都是新的，时间轴从 0 开始',
          catalog: window.RESCUE_TRACE_CATALOG || []
        };
        loadApp();
        return;
      } catch (e2) {
        /* 解析失败就当没这回事，退回目录里的场景 */
      }
    }
  }

  var catalog = window.RESCUE_TRACE_CATALOG || [];
  var file = 'trace.js';            /* 旧部署的兜底 */
  var key = params.get('trace');

  if (catalog.length) {
    scene.catalog = catalog;
    var hit = null;
    var i;
    if (key) {
      for (i = 0; i < catalog.length; ++i) {
        if (catalog[i].key === key) { hit = catalog[i]; break; }
      }
    }
    if (!hit) hit = catalog[0];                 /* 没传/传错 → 用第一个场景 */
    file = hit.file;
    scene.key = hit.key;
    scene.title = hit.title;
    scene.desc = hit.desc;
    /* URL 上没写 trace 时补上，方便复制链接分享 */
    if (!key) {
      try {
        var u = new URL(location.href);
        u.searchParams.set('trace', hit.key);
        history.replaceState(null, '', u.toString());
      } catch (e) { /* 某些浏览器在 file:// 下限制 replaceState，忽略即可 */ }
    }
  }

  window.__RESCUE_SCENE = scene;

  var sc = document.createElement('script');
  sc.src = file;
  sc.onload = loadApp;
  sc.onerror = function () {
    showError('数据文件加载失败：<code style="color:#ffb245">' + file + '</code>');
  };
  document.body.appendChild(sc);
})();
