/* 由 viz_test --emit-all 自动生成（测试桩数据，勿手工编辑）。
 * 每场景一份 trace.js，页面按 ?trace=<key> 加载。 */
window.RESCUE_TRACE_CATALOG = [
  { "key": "standard", "title": "标准场景 · 9 目标", "desc": "目标分散成一圈，生存时间宽裕，先看整体效果",
    "file": "traces/standard/trace.js" },
  { "key": "tight", "title": "紧急场景 · 12 目标", "desc": "生存时间紧：顺序选错会大量牺牲（切到反向/交替基线看红色）",
    "file": "traces/tight/trace.js" },
  { "key": "cluster", "title": "星团场景 · 10 目标", "desc": "目标挤成一团且离基点很远，航程占比高",
    "file": "traces/cluster/trace.js" },
  { "key": "wide", "title": "大尺度场景 · 8 目标", "desc": "半径很大、生存时间很宽，考察长航段调度",
    "file": "traces/wide/trace.js" },
  { "key": "random", "title": "随机场景 · 11 目标", "desc": "半径与生存时间都居中，结果随参数变化",
    "file": "traces/random/trace.js" },
  { "key": "max", "title": "满规模 · 12 目标", "desc": "目标数打满 MAX_TARGET_NUM，最拥挤的一局",
    "file": "traces/max/trace.js" }
];
